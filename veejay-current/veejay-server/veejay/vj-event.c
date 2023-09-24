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
#include <SDL2/SDL.h>
#endif
#include <stdarg.h>
#include <veejaycore/defs.h>
#include <veejaycore/hash.h>
#include <libvje/vje.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <libsubsample/subsample.h>
#include <veejay/vj-lib.h>
#include <veejay/vj-perform.h>
#include <veejay/libveejay.h>
#include <veejay/vj-viewport.h>
#include <veejay/vj-composite.h>
#include <veejay/vj-shm.h>
#include <veejay/vj-macro.h>
#include <veejay/vj-sdl.h>
#include <libel/vj-avcodec.h>
#include <libsamplerec/samplerecord.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <veejaycore/vims.h>
#include <veejaycore/yuvconv.h>
#include <veejay/vj-event.h>
#ifdef HAVE_JACK
#include <veejay/vj-jack.h>
#endif
#include <libstream/vj-tag.h>
#include <libstream/vj-vloopback.h>
#include <veejaycore/lzo.h>
#include <veejay/vjkf.h>
#ifdef HAVE_GL
#include <veejay/gl.h>
#endif
#ifdef USE_GDK_PIXBUF
#include <libel/pixbuf.h>
#endif
#include <veejaycore/vevo.h>
#include <veejaycore/libvevo.h>
#include <veejay/vj-OSC.h>
#include <veejaycore/vj-server.h>
#include <veejay/vj-share.h>
#include <veejay/vevo.h>
#include <veejay/vj-misc.h>
#include <libvjxml/vj-xml.h>
/* Highest possible SDL Key identifier */
#define MAX_SDL_KEY (3 * SDL_SCANCODE_LAST) + 1  
#define MSG_MIN_LEN   4 /* stripped ';' */
#ifdef HAVE_FREETYPE
#include <veejay/vj-font.h>
#endif

#include <libstream/vj-net.h>

#ifdef HAVE_V4L2
#include <libstream/v4l2utils.h>
#endif

#include <libplugger/plugload.h>
#include <libvje/internal.h>
#include <libvje/libvje.h>

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#define MAX_ARGUMENTS (SAMPLE_MAX_PARAMETERS + 8)

static int use_bw_preview_ = 0;
static int _last_known_num_args = 0;
static hash_t *BundleHash = NULL;
static uint8_t *sample_image_buffer = NULL;

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
    int list_id;            // VIMS id
    vj_event act;           // function pointer
} vj_events;

static  vj_events   net_list[VIMS_MAX];
static  int     override_keyboard = 0;
#ifdef HAVE_SDL
typedef struct
{
    vj_events   *vims;
    int     key_symbol;
    int     key_mod;
    int     arg_len;
    char        *arguments;
    int     event_id;
} vj_keyboard_event;

static hash_t *keyboard_events = NULL;
static hash_t *keyboard_eventid_map = NULL;


// maximum number of key combinations
#define MAX_KEY_MNE (SDL_NUM_SCANCODES * 16)
static  vj_keyboard_event *keyboard_event_map_[MAX_KEY_MNE];

typedef struct
{
    int key_symbol;
    int key_mod;
    char    *args;
    int arg_len;
    void    *next;
} vims_key_list;

#endif

static int _recorder_format = ENCODER_MJPEG;

#define SEND_BUF 256000

static  char    *get_print_buf(int size) {
    int s = size;
    if( s<= 0)
        s = SEND_BUF;
    char *res = (char*) vj_calloc(sizeof(char) * RUP8(s) );
    return res;
}

#define MAX_VIMS_ARGUMENTS 16

static void vj_event_sample_next1( veejay_t *v );

extern void veejay_pipe_write_status(veejay_t *info);
extern int  _vj_server_del_client(vj_server * vje, int link_id);
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
typedef enum { //WARNING ; on change this think to update keyboard_event_map_[] !!!!
  VIMS_MOD_NONE  = 0x0000,
  VIMS_MOD_ALT= 0x0001,
  VIMS_MOD_CTRL= 0x0002,
  VIMS_MOD_SHIFT = 0x0004,
  VIMS_MOD_CAPSLOCK = 0x0008,
} KEYMod;

#define VIMS_MOD_ALT_SHIFT        VIMS_MOD_ALT|VIMS_MOD_SHIFT
#define VIMS_MOD_CTRL_SHIFT       VIMS_MOD_CTRL|VIMS_MOD_SHIFT
#define VIMS_MOD_CTRL_ALT         VIMS_MOD_CTRL|VIMS_MOD_ALT
#define VIMS_MOD_CTRL_ALT_SHIFT   VIMS_MOD_CTRL|VIMS_MOD_ALT|VIMS_MOD_SHIFT

static struct {                 /* hardcoded keyboard layout (the default keys) */
    int event_id;           
    int key_sym;            
    KEYMod key_mod;
    const char *value;
} vj_event_default_sdl_keys[] = {

    { 0,0,0,NULL },
    { VIMS_PROJ_INC,                SDL_SCANCODE_LEFT,      VIMS_MOD_CTRL, "-1 0"   },
    { VIMS_PROJ_INC,                SDL_SCANCODE_RIGHT,     VIMS_MOD_CTRL, "1 0"    },
    { VIMS_PROJ_INC,                SDL_SCANCODE_UP,        VIMS_MOD_CTRL, "0 -1"   },
    { VIMS_PROJ_INC,                SDL_SCANCODE_DOWN,      VIMS_MOD_CTRL, "0 1"    },
    { VIMS_EFFECT_SET_BG,           SDL_SCANCODE_B,         VIMS_MOD_ALT,   NULL    },
    { VIMS_VIDEO_PLAY_FORWARD,      SDL_SCANCODE_KP_6,       VIMS_MOD_NONE,  NULL    },
    { VIMS_VIDEO_PLAY_BACKWARD,     SDL_SCANCODE_KP_4,       VIMS_MOD_NONE,  NULL    },
    { VIMS_VIDEO_PLAY_STOP,         SDL_SCANCODE_KP_5,       VIMS_MOD_NONE,  NULL    },
    { VIMS_VIDEO_PLAY_STOP_ALL,	    SDL_SCANCODE_KP_5,	    VIMS_MOD_SHIFT, NULL    },
    { VIMS_VIDEO_SKIP_FRAME,        SDL_SCANCODE_KP_9,       VIMS_MOD_NONE,  "1" },
    { VIMS_VIDEO_PREV_FRAME,        SDL_SCANCODE_KP_7,       VIMS_MOD_NONE,  "1" },
    { VIMS_VIDEO_SKIP_SECOND,       SDL_SCANCODE_KP_8,       VIMS_MOD_NONE,  NULL    },
    { VIMS_VIDEO_PREV_SECOND,       SDL_SCANCODE_KP_2,       VIMS_MOD_NONE,  NULL    },
    { VIMS_VIDEO_GOTO_START,        SDL_SCANCODE_KP_1,       VIMS_MOD_NONE,  NULL    },
    { VIMS_VIDEO_GOTO_END,          SDL_SCANCODE_KP_3,       VIMS_MOD_NONE,  NULL    },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_A,         VIMS_MOD_NONE,  "1" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_S,         VIMS_MOD_NONE,  "2" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_D,         VIMS_MOD_NONE,  "3" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_F,         VIMS_MOD_NONE,  "4" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_G,         VIMS_MOD_NONE,  "5" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_H,         VIMS_MOD_NONE,  "6" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_J,         VIMS_MOD_NONE,  "7" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_K,         VIMS_MOD_NONE,  "8" },
    { VIMS_VIDEO_SET_SPEEDK,        SDL_SCANCODE_L,         VIMS_MOD_NONE,  "9" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_A,         VIMS_MOD_ALT,   "1" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_S,         VIMS_MOD_ALT,   "2" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_D,         VIMS_MOD_ALT,   "3" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_F,         VIMS_MOD_ALT,   "4" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_G,         VIMS_MOD_ALT,   "5" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_H,         VIMS_MOD_ALT,   "6" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_J,         VIMS_MOD_ALT,   "7" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_K,         VIMS_MOD_ALT,   "8" },
    { VIMS_VIDEO_SET_SLOW,          SDL_SCANCODE_L,         VIMS_MOD_ALT,   "9"    },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_A,         VIMS_MOD_SHIFT,   "1" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_S,         VIMS_MOD_SHIFT,   "2" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_D,         VIMS_MOD_SHIFT,   "3" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_F,         VIMS_MOD_SHIFT,   "4" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_G,         VIMS_MOD_SHIFT,   "5" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_H,         VIMS_MOD_SHIFT,   "6" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_J,         VIMS_MOD_SHIFT,   "7" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_K,         VIMS_MOD_SHIFT,   "8" },
    { VIMS_SAMPLE_MIX_SET_SPEED,    SDL_SCANCODE_L,         VIMS_MOD_SHIFT,   "9" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_A,         VIMS_MOD_ALT_SHIFT,   "1" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_S,         VIMS_MOD_ALT_SHIFT,   "2" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_D,         VIMS_MOD_ALT_SHIFT,   "3" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_F,         VIMS_MOD_ALT_SHIFT,   "4" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_G,         VIMS_MOD_ALT_SHIFT,   "5" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_H,         VIMS_MOD_ALT_SHIFT,   "6" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_J,         VIMS_MOD_ALT_SHIFT,   "7" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_K,         VIMS_MOD_ALT_SHIFT,   "8" },
    { VIMS_SAMPLE_MIX_SET_DUP,      SDL_SCANCODE_L,         VIMS_MOD_ALT_SHIFT,   "9" },
    #ifdef HAVE_SDL
    { VIMS_FULLSCREEN,              SDL_SCANCODE_F,         VIMS_MOD_CTRL,  "2"    },
#endif  
    { VIMS_CHAIN_ENTRY_DOWN,        SDL_SCANCODE_KP_MINUS,  VIMS_MOD_NONE,  "1" },
    { VIMS_CHAIN_ENTRY_UP,          SDL_SCANCODE_KP_PLUS,   VIMS_MOD_NONE,  "1" },
    { VIMS_CHAIN_ENTRY_CHANNEL_INC, SDL_SCANCODE_EQUALS,    VIMS_MOD_NONE,  NULL    },
    { VIMS_CHAIN_ENTRY_CHANNEL_DEC, SDL_SCANCODE_MINUS,     VIMS_MOD_NONE,  NULL    },
    { VIMS_CHAIN_ENTRY_SOURCE_TOGGLE,SDL_SCANCODE_SLASH,    VIMS_MOD_NONE,  NULL    }, // stream/sample
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_PAGEUP,    VIMS_MOD_NONE,  "0 1"   },
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_KP_PERIOD, VIMS_MOD_NONE,  "1 1"   },
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_PERIOD,    VIMS_MOD_NONE,  "2 1"   },
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_W,         VIMS_MOD_NONE,  "3 1"   },
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_R,         VIMS_MOD_NONE,  "4 1"   },
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_Y,         VIMS_MOD_NONE,  "5 1"   },
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_I,         VIMS_MOD_NONE,  "6 1"   },
    { VIMS_CHAIN_ENTRY_INC_ARG,     SDL_SCANCODE_P,         VIMS_MOD_NONE,  "7 1"   },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_PAGEDOWN,  VIMS_MOD_NONE,  "0 -1"  },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_KP_0,       VIMS_MOD_NONE,  "1 -1"  },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_COMMA,     VIMS_MOD_NONE,  "2 -1"  },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_Q,         VIMS_MOD_NONE,  "3 -1"  },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_E,         VIMS_MOD_NONE,  "4 -1"  },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_T,         VIMS_MOD_NONE,  "5 -1"  },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_U,         VIMS_MOD_NONE,  "6 -1"  },
    { VIMS_CHAIN_ENTRY_DEC_ARG,     SDL_SCANCODE_O,         VIMS_MOD_NONE,  "7 -1"  },
    { VIMS_TOGGLE_TRANSITIONS,      SDL_SCANCODE_T,         VIMS_MOD_SHIFT, NULL    },
    { VIMS_OSD,                     SDL_SCANCODE_O,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_COPYRIGHT,               SDL_SCANCODE_C,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_COMPOSITE,               SDL_SCANCODE_I,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_OSD_EXTRA,               SDL_SCANCODE_H,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_PROJ_STACK,              SDL_SCANCODE_V,         VIMS_MOD_CTRL,  "1 0"   },
    { VIMS_PROJ_STACK,              SDL_SCANCODE_P,         VIMS_MOD_CTRL,  "0 1"   },
    { VIMS_PROJ_TOGGLE,             SDL_SCANCODE_A,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_FRONTBACK,               SDL_SCANCODE_S,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_RENDER_DEPTH,            SDL_SCANCODE_D,         VIMS_MOD_CTRL,  "2" },
  /*  { VIMS_SELECT_BANK,             SDL_SCANCODE_1,         VIMS_MOD_NONE,  "1" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_2,         VIMS_MOD_NONE,  "2" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_3,         VIMS_MOD_NONE,  "3" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_4,         VIMS_MOD_NONE,  "4" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_5,         VIMS_MOD_NONE,  "5" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_6,         VIMS_MOD_NONE,  "6" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_7,         VIMS_MOD_NONE,  "7" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_8,         VIMS_MOD_NONE,  "8" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_9,         VIMS_MOD_NONE,  "9" }, */
    { VIMS_SELECT_BANK,             SDL_SCANCODE_1,         VIMS_MOD_SHIFT, "1" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_2,         VIMS_MOD_SHIFT, "2" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_3,         VIMS_MOD_SHIFT, "3" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_4,         VIMS_MOD_SHIFT, "4" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_5,         VIMS_MOD_SHIFT, "5" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_6,         VIMS_MOD_SHIFT, "6" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_7,         VIMS_MOD_SHIFT, "7" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_8,         VIMS_MOD_SHIFT, "8" },
    { VIMS_SELECT_BANK,             SDL_SCANCODE_9,         VIMS_MOD_SHIFT, "9" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_0,      VIMS_MOD_NONE, "0" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_1,      VIMS_MOD_NONE, "10" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_2,      VIMS_MOD_NONE, "20" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_3,      VIMS_MOD_NONE, "30" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_4,      VIMS_MOD_NONE, "40" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_5,      VIMS_MOD_NONE, "50" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_6,      VIMS_MOD_NONE, "60" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_7,      VIMS_MOD_NONE, "70" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_8,      VIMS_MOD_NONE, "80" },
    { VIMS_VIDEO_SET_FRAME_PERCENTAGE, SDL_SCANCODE_9,      VIMS_MOD_NONE, "90" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F1,        VIMS_MOD_NONE,  "1" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F2,        VIMS_MOD_NONE,  "2" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F3,        VIMS_MOD_NONE,  "3" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F4,        VIMS_MOD_NONE,  "4" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F5,        VIMS_MOD_NONE,  "5" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F6,        VIMS_MOD_NONE,  "6" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F7,        VIMS_MOD_NONE,  "7" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F8,        VIMS_MOD_NONE,  "8" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F9,        VIMS_MOD_NONE,  "9" },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F10,       VIMS_MOD_NONE,  "10"    },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F11,       VIMS_MOD_NONE,  "11"    },
    { VIMS_SELECT_ID,               SDL_SCANCODE_F12,       VIMS_MOD_NONE,  "12"    },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F1,        VIMS_MOD_SHIFT,  "1" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F2,        VIMS_MOD_SHIFT,  "2" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F3,        VIMS_MOD_SHIFT,  "3" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F4,        VIMS_MOD_SHIFT,  "4" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F5,        VIMS_MOD_SHIFT,  "5" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F6,        VIMS_MOD_SHIFT,  "6" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F7,        VIMS_MOD_SHIFT,  "7" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F8,        VIMS_MOD_SHIFT,  "8" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F9,        VIMS_MOD_SHIFT,  "9" },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F10,       VIMS_MOD_SHIFT,  "10"    },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F11,       VIMS_MOD_SHIFT,  "11"    },
    { VIMS_RESUME_ID,               SDL_SCANCODE_F12,       VIMS_MOD_SHIFT,  "12"    },
    { VIMS_SET_PLAIN_MODE,          SDL_SCANCODE_KP_DIVIDE, VIMS_MOD_NONE,  NULL    },
    { VIMS_REC_AUTO_START,          SDL_SCANCODE_E,         VIMS_MOD_CTRL,  "100"   },
    { VIMS_REC_STOP,                SDL_SCANCODE_T,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_REC_START,               SDL_SCANCODE_R,         VIMS_MOD_CTRL,  NULL    },
    { VIMS_CHAIN_TOGGLE,            SDL_SCANCODE_END,       VIMS_MOD_NONE,  NULL    },
    { VIMS_CHAIN_ENTRY_SET_STATE,   SDL_SCANCODE_END,       VIMS_MOD_ALT,   NULL    },  
    { VIMS_CHAIN_ENTRY_CLEAR,       SDL_SCANCODE_DELETE,    VIMS_MOD_NONE,  NULL    },
    { VIMS_FXLIST_INC,              SDL_SCANCODE_UP,        VIMS_MOD_NONE,  "1" },
    { VIMS_FXLIST_DEC,              SDL_SCANCODE_DOWN,      VIMS_MOD_NONE,  "1" },
    { VIMS_FXLIST_ADD,              SDL_SCANCODE_RETURN,    VIMS_MOD_NONE,  NULL    },
    { VIMS_SET_SAMPLE_START,        SDL_SCANCODE_LEFTBRACKET,VIMS_MOD_NONE, NULL    },
    { VIMS_SET_SAMPLE_END,          SDL_SCANCODE_RIGHTBRACKET,VIMS_MOD_NONE,    NULL    },
    { VIMS_SAMPLE_SET_MARKER_START, SDL_SCANCODE_LEFTBRACKET,VIMS_MOD_ALT,  NULL    },
    { VIMS_SAMPLE_SET_MARKER_END,   SDL_SCANCODE_RIGHTBRACKET,VIMS_MOD_ALT, NULL    },
    { VIMS_SAMPLE_TOGGLE_LOOP,      SDL_SCANCODE_KP_MULTIPLY,VIMS_MOD_NONE,NULL },
    { VIMS_SAMPLE_TOGGLE_RAND_LOOP, SDL_SCANCODE_KP_MULTIPLY, VIMS_MOD_SHIFT },
    { VIMS_SWITCH_SAMPLE_STREAM,    SDL_SCANCODE_ESCAPE,    VIMS_MOD_NONE, NULL },
    { VIMS_PRINT_INFO,              SDL_SCANCODE_HOME,      VIMS_MOD_NONE, NULL },
    { VIMS_OSL,                     SDL_SCANCODE_HOME,      VIMS_MOD_CTRL, NULL },
    { VIMS_SAMPLE_CLEAR_MARKER,     SDL_SCANCODE_BACKSPACE, VIMS_MOD_NONE, NULL },
    { VIMS_MACRO,                   SDL_SCANCODE_SPACE,     VIMS_MOD_NONE, "2 1"    },
    { VIMS_MACRO,                   SDL_SCANCODE_SPACE,     VIMS_MOD_SHIFT,  "1 1"  },
    { VIMS_MACRO,                   SDL_SCANCODE_SPACE,     VIMS_MOD_CTRL, "0 0"    },
    { VIMS_MACRO,                   SDL_SCANCODE_SPACE,     VIMS_MOD_CAPSLOCK, "3 1"},
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F1,        VIMS_MOD_CTRL, "0"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F2,        VIMS_MOD_CTRL, "1"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F3,        VIMS_MOD_CTRL, "2"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F4,        VIMS_MOD_CTRL, "3"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F5,        VIMS_MOD_CTRL, "4"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F6,        VIMS_MOD_CTRL, "5"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F7,        VIMS_MOD_CTRL, "6"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F8,        VIMS_MOD_CTRL, "7"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F9,        VIMS_MOD_CTRL, "8"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F10,       VIMS_MOD_CTRL, "9"  },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F11,       VIMS_MOD_CTRL, "10" },
    { VIMS_MACRO_SELECT,            SDL_SCANCODE_F12,       VIMS_MOD_CTRL, "11" },
    { VIMS_SAMPLE_HOLD_FRAME,       SDL_SCANCODE_PAUSE,     VIMS_MOD_NONE, "0 0 5" },
    { 0,0,0,NULL },
};
#endif

#define VIMS_REQUIRE_ALL_PARAMS (1<<0)          /* all params needed */
#define VIMS_DONT_PARSE_PARAMS (1<<1)       /* dont parse arguments */
#define VIMS_LONG_PARAMS (1<<3)             /* long string arguments (bundle, plugin) */
#define VIMS_ALLOW_ANY (1<<4)               /* use defaults when optional arguments are not given */            

#define FORMAT_MSG(dst,str) sprintf(dst,"%03zu%s",strlen(str),str)
#define APPEND_MSG(dst,str) veejay_strncat(dst,str,strlen(str))
#define SEND_MSG_DEBUG(v,str) \
{\
char *__buf = str;\
int  __len = strlen(str);\
int  __done = 0;\
veejay_msg(VEEJAY_MSG_INFO, "--------------------------------------------------------");\
for(__done = 0; __len > (__done + 80); __done += 80)\
{\
    char *__tmp = vj_strndup( str+__done, 80 );\
veejay_msg(VEEJAY_MSG_INFO, "[%zu][%s]",strlen(str),__tmp);\
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
    _vj_server_del_client( v->vjs[VEEJAY_PORT_DAT], v->uc->current_link); \
    return;\
    }\
}

#define SEND_MSG(v,str)\
{\
int bf_len = strlen(str);\
    if(bf_len && vj_server_send(v->vjs[VEEJAY_PORT_CMD], v->uc->current_link, (uint8_t*) str, bf_len) < 0) { \
    _vj_server_del_client( v->vjs[VEEJAY_PORT_CMD], v->uc->current_link); \
    _vj_server_del_client( v->vjs[VEEJAY_PORT_STA], v->uc->current_link); \
    _vj_server_del_client( v->vjs[VEEJAY_PORT_DAT], v->uc->current_link);} \
}


/* some macros for commonly used checks */
#define SAMPLE_PLAYING(v) ( (v->uc->playback_mode == VJ_PLAYBACK_MODE_SAMPLE) || (v->rmodes[v->uc->current_link] == VJ_PLAYBACK_MODE_SAMPLE) )
#define STREAM_PLAYING(v) ( (v->uc->playback_mode == VJ_PLAYBACK_MODE_TAG) || (v->rmodes[v->uc->current_link] == VJ_PLAYBACK_MODE_TAG))
#define PLAIN_PLAYING(v) ( (v->uc->playback_mode == VJ_PLAYBACK_MODE_PLAIN) || (v->rmodes[v->uc->current_link] == VJ_PLAYBACK_MODE_PLAIN))

#define p_no_sample(a) {  veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist",a); }
#define p_no_tag(a)    {  veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist",a); }
#define p_invalid_mode() {  veejay_msg(VEEJAY_MSG_DEBUG, "Invalid playback mode for this action"); }
#define v_chi(v) ( (v < 0  || v >= SAMPLE_MAX_EFFECTS ) ) 

#define SAMPLE_DEFAULTS(args) {\
\
 if(args == -1) args = sample_highest_valid_id();\
 if(args == 0) args = v->uc->sample_id;\
}

#define STREAM_DEFAULTS(args) {\
\
 if(args == -1) args = vj_tag_highest_valid_id();\
 if(args == 0) args = v->uc->sample_id;\
}


static inline void P_A(int *args, size_t argsize, char *str, size_t strsize, const char *format, va_list ap)
{
	unsigned int index;
	int num_args = (argsize > 0 ? argsize / sizeof(int) : 0);

#ifdef STRICT_CHECKING
	if( args == NULL ) {
		assert( argsize == 0 );
	}
	if( str == NULL ) {
		assert( strsize == 0 );
	}
	if( argsize > 0 ) {
		assert(args != NULL);
	}
	if( strsize > 0 ) {
		assert(str != NULL);
	}
#endif

	for( index = 0; index < num_args ; index ++ )
			args[index] = 0;

	index = 0;

	while(*format) {
		switch(*format++) {
			case 's':
				if( str == NULL )
					break;
				veejay_memset( str, 0, strsize );
				char *tmp = (char*)va_arg(ap, char*);
				if(tmp != NULL ) {
					int tmplen= strlen(tmp);
					if(tmplen > strsize) {
							veejay_msg(VEEJAY_MSG_WARNING, "Truncated user input (%d bytes needed, have room for %d bytes)", tmplen, strsize);
							tmplen = strsize;
					}
					strncpy( str, tmp, tmplen );
				}
				break;
			case 'd':
				if( args == NULL)
					break;
				args[index] = *( va_arg(ap, int*));
				index ++;
				break;
		}
	}

}

#define CLAMPVAL(a) { if(a<0)a=0; else if(a >255) a =255; }

static 	void	vj_event_macro_get_loop_dup( veejay_t *v, int *at_dup, int *at_loop )
{
	if( SAMPLE_PLAYING(v)) {
    	*at_dup = sample_get_framedups( v->uc->sample_id );
	    *at_loop = sample_get_loop_stats( v->uc->sample_id );
	}
    else if (STREAM_PLAYING(v)) {
        *at_dup = 0;
        *at_loop = vj_tag_get_loop_stats( v->uc->sample_id );
    }
    else if ( PLAIN_PLAYING(v)) {
    	*at_dup = v->settings->simple_frame_dup;
		*at_loop = 0;
	}
}

static hash_val_t int_bundle_hash(const void *key)
{
    return (hash_val_t) key;
}

static int int_bundle_compare(const void *key1,const void *key2)
{
#ifdef ARCH_X86_64
    return ((uint64_t) key1 < (uint64_t) key2 ? -1 :
            ((uint64_t) key1 < (uint64_t) key2 ? 1: 0 ));
#else
    return ((uint32_t)key1 < (uint32_t) key2 ? -1 : 
        ((uint32_t) key1 > (uint32_t) key2 ? +1 : 0));
#endif
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
int keyboard_event_exists(int id);
int del_keyboard_event(int id );
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
int vj_has_video(veejay_t *v, editlist *el);
void vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int type);
void    vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod);
#ifdef HAVE_SDL
static vims_key_list * vj_event_get_keys( int event_id );
int vj_event_single_fire(void *ptr , SDL_Event event, int pressed);
int vj_event_register_keyb_event(int event_id, int key_id, int key_mod, char *args);
void vj_event_unregister_keyb_event(int key_id, int key_mod);
#endif

#ifdef HAVE_XML2
void    vj_event_format_xml_event( xmlNodePtr node, int event_id );
//void  vj_event_format_xml_stream( xmlNodePtr node, int stream_id );
#endif

int vj_has_video(veejay_t *v,editlist *el)
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
#ifdef ARCH_X86_64
        uint64_t tid = (uint64_t) bundle_id;
#else
        uint32_t tid = (uint32_t) bundle_id;
#endif

        hnode_put( n, (void*) tid);
        hnode_destroy(n);
        return 1;
    }
    return 0;
}

static  void    constrain_sample( veejay_t *v,int n )
{
    vj_font_set_dict(v->font, sample_get_dict(n) );
    //  v->current_edit_list->video_fps,
    vj_font_prepare( v->font, sample_get_startFrame(n),
            sample_get_endFrame(n) );

}

static  void    constrain_stream( veejay_t *v, int n, long hi )
{
    vj_font_set_dict(v->font, vj_tag_get_dict(n) );
    //  v->current_edit_list->video_fps,
    vj_font_prepare( v->font, 0, vj_tag_get_n_frames(n) );
}

vj_msg_bundle *vj_event_bundle_get(int event_id)
{
    vj_msg_bundle *m;
#ifdef ARCH_X86_64
    uint64_t tid = (uint64_t) event_id;
#else
    uint32_t tid = (uint32_t) event_id;
#endif

    hnode_t *n = hash_lookup(BundleHash, (void*) tid);
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
int         del_keyboard_event(int id )
{
    hnode_t *node;
    vj_keyboard_event *ev = get_keyboard_event( id );

    keyboard_event_map_[ id ] = NULL;
#ifdef ARCH_X86_64
    uint64_t tid = (uint64_t) id;
#else
    uint32_t tid = (uint32_t) id;
#endif

    if(ev == NULL)
        return 0;
    node = hash_lookup( keyboard_events, (void*) tid );
    if(!node)
        return 0;
    if(ev->arguments)
        free(ev->arguments);
    if(ev->vims )
        free(ev->vims );
    hash_delete( keyboard_events, node );

    return 1;  
}

vj_keyboard_event   *get_keyboard_event(int id)
{
    if( id < 0 || id > MAX_KEY_MNE ) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Keybinding %d does not fit in keyboard map", id );
        return NULL;
    }   

    return keyboard_event_map_[id];
}

int     keyboard_event_exists(int id)
{
#ifdef ARCH_X86_64
    uint64_t tid = (uint64_t) id;
#else
    uint32_t tid = (uint32_t) id;
#endif

    hnode_t *node = hash_lookup( keyboard_events, (void*) tid );
    if(node)
        if( hnode_get(node) != NULL )
            return 1;
    return 0;
}

static void destroy_keyboard_event( vj_keyboard_event *ev )
{
    if( ev ) {
        if( ev->vims ) {
            free( ev->vims );
		}

        if( ev->arguments ) {
            free( ev->arguments );  
		}

        free(ev);
    }
}

static void configure_vims_key_event(vj_keyboard_event *ev, int symbol, int modifier, int event_id, const char *value)
{
    ev->arg_len = 0;
    ev->arguments = NULL;

    if(value)
    {
        ev->arg_len = strlen(value);
        ev->arguments = vj_strndup( value, ev->arg_len );
    }
    else
    {
        if(event_id < VIMS_BUNDLE_START || event_id > VIMS_BUNDLE_END)
        {
            ev->arguments = find_keyboard_default( event_id );
            if(ev->arguments)
                ev->arg_len = strlen(ev->arguments);
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
    else
    {
        ev->vims->act = vj_event_none;
        ev->vims->list_id = event_id;
    }

    ev->key_symbol = symbol;
    ev->key_mod = modifier;
    ev->event_id = event_id;

}

vj_keyboard_event *new_keyboard_event(
        int symbol, int modifier, const char *value, int event_id )
{
    if( event_id <= 0 )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "VIMS event %d does not exist", event_id );
        return NULL;
    }

    vj_keyboard_event *ev = (vj_keyboard_event*)vj_calloc(sizeof(vj_keyboard_event));
    if(!ev)
        return NULL;
    ev->vims = (vj_events*) vj_calloc(sizeof(vj_events));
    if(!ev->vims)
        return NULL;

    keyboard_event_map_ [ (modifier * SDL_NUM_SCANCODES) + symbol ] = ev;

    configure_vims_key_event( ev,symbol,modifier, event_id, value );

    return ev;
}
#endif

int vj_event_bundle_exists(int event_id)
{
#ifdef ARCH_X86_64
    uint64_t tid = (uint64_t) event_id;
#else
    uint32_t tid = (uint32_t) event_id;
#endif

    hnode_t *n = hash_lookup( BundleHash,(void*) tid );
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
#ifdef ARCH_X86_64
    uint64_t tid = (uint64_t) m->event_id;
#else
    uint32_t tid = (uint32_t) m->event_id;
#endif
    if(!vj_event_bundle_exists(m->event_id))
    {
        hash_insert( BundleHash, n, (void*) tid);
    }
    else
    {
        hnode_put( n, (void*) tid);
        hnode_destroy( n );
    }

    // add bundle to VIMS list
    veejay_msg(VEEJAY_MSG_DEBUG, "Added VIMS Bundle %d", m->event_id );
 
    net_list[ m->event_id ].list_id = m->event_id;
    net_list[ m->event_id ].act = vj_event_none;
    return 1;
}

int vj_event_bundle_del( int event_id )
{
    hnode_t *n;
    vj_msg_bundle *m = vj_event_bundle_get( event_id );
    if(!m) return -1;
#ifdef ARCH_X86_64
    uint64_t tid = (uint64_t) event_id;
#else
    uint32_t tid = (uint32_t) event_id;
#endif

    n = hash_lookup( BundleHash, (void*) tid );
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
        veejay_msg(VEEJAY_MSG_ERROR, "A VIMS Bundle must have some contents");
        return NULL;
    }   
    len = strlen(bundle_msg);
    m = (vj_msg_bundle*) vj_calloc(sizeof(vj_msg_bundle));
    if(!m) 
    {
        veejay_msg(VEEJAY_MSG_DEBUG, "Error allocating memory for bundled message");
        return NULL;
    }
    m->bundle = (char*) vj_calloc(sizeof(char) * len+1);
    m->accelerator = 0;
    m->modifier = 0;
    if(!m->bundle)
    {
        veejay_msg(VEEJAY_MSG_DEBUG, "Error allocating memory for bundled message context");
        return NULL;
    }
    veejay_strncpy(m->bundle, bundle_msg, len);
    
    m->event_id = event_id;

    veejay_msg(VEEJAY_MSG_DEBUG, 
        "(VIMS) New VIMS Bundle %d [%s] created",
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
static void vj_event_parse_kf( veejay_t *v, char *msg, int len )
{
    if(SAMPLE_PLAYING(v))
    {
        if(sample_chain_set_kfs( v->uc->sample_id, len, msg )==-1)
            veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Invalid key frame blob [%s]",msg);
    }
    else if (STREAM_PLAYING(v))
    {
        if(vj_tag_chain_set_kfs(v->uc->sample_id,len,(unsigned char*)msg ) == -1)
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
    char atomic_msg[256];
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
            veejay_msg(VEEJAY_MSG_ERROR,"(VIMS) Invalid number of messages given to execute. Skipping message [%s]",msg);
            return;
        }

        offset += 3;

        if ( msg[offset] != '{' )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "(VIMS) Expected a left bracket at position %d. Skipping message [%s]",offset,msg);
            return;
        }   

        offset += 1;    /* skip # */

        for( i = 1; i <= num_msg ; i ++ )
        {               
            int found_end_of_msg = 0;
            int total_msg_len = strlen(msg);

            veejay_memset( atomic_msg, 0 , sizeof(atomic_msg) ); /* clear */

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

static  void    dump_arguments_(int net_id,int arglen, int np, int prefixed, char *fmt)
{
    int i;
    char *name = vj_event_vevo_get_event_name( net_id );
    veejay_msg(VEEJAY_MSG_ERROR, "VIMS '%03d' : '%s'", net_id, name );
    veejay_msg(VEEJAY_MSG_ERROR, "\tWrong number of arguments, got %d, need %d",arglen,np);   
    veejay_msg(VEEJAY_MSG_ERROR, "\tFormat is '%s'", fmt );

    for( i = prefixed; i < np; i ++ )
    {
        char *help = vj_event_vevo_help_vims( net_id, i );
        veejay_msg(VEEJAY_MSG_ERROR,"\t\tArgument %d : %s",
            i,help );
        if(help) free(help);
    }

    free(name);
}

static  void    dump_argument_( int net_id , int i )
{
    char *help = vj_event_vevo_help_vims( net_id, i );
        veejay_msg(VEEJAY_MSG_ERROR,"\t\tArgument %d : %s",
            i,help );
    if(help) free(help);
}

static  int vj_event_verify_args( int *fx, int net_id , int arglen, int np, int prefixed, char *fmt )
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
        if( arglen < 3 )
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
    }
    return 1;
}

void    vj_event_fire_net_event(veejay_t *v, int net_id, char *str_arg, int *args, int arglen, int prefixed)
{
    int np = vj_event_vevo_get_num_args(net_id);
    char *fmt = vj_event_vevo_get_event_format( net_id );
    int flags = vj_event_vevo_get_flags( net_id );
    int fmt_offset = 1; 
    vims_arg_t  vims_arguments[MAX_VIMS_ARGUMENTS];

    veejay_memset(vims_arguments, 0, sizeof(vims_arguments));

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
        else if( fmt[fmt_offset] == 's' )
        {
            if(str_arg == NULL )
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Argument %d must be a string! (VIMS %03d)", i,net_id );
                if(fmt) free(fmt);
                return;
            }
            vims_arguments[i].value = (void*) vj_strdup( str_arg );
            if(flags & VIMS_REQUIRE_ALL_PARAMS )
            {
                if( strlen((char*)vims_arguments[i].value) <= 0 )
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "Argument %d is not a string",i );
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
	fmt_offset += 3;
    }

    while( i < MAX_VIMS_ARGUMENTS ) {
        vims_arguments[i].value = 0;
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
        if( vims_arguments[i].value && fmt[fmt_offset] == 's' )
            free( vims_arguments[i].value );
        fmt_offset += 3;
    }
    if(fmt)
        free(fmt);

}

static      int inline_str_to_int(const char *msg, int *val)
{
    int str_len = 0;
    if( sscanf( msg , "%d%n", val,&str_len ) <= 0 )
        return 0;
    return str_len;
}

static      char    *inline_str_to_str(int flags, char *msg)
{
    char *res = NULL;   
    int len = strlen(msg);
    if( len <= 0 )
        return NULL;

    if( (flags & VIMS_LONG_PARAMS) ) /* copy rest of message */
    {
        res = (char*) vj_calloc(sizeof(char) * (len+1) );
        veejay_strncpy( res, msg, len );
    }
    else            
    {
        char str[256];
        if(sscanf( msg, "%256s", str ) <= 0 )
            return NULL;
        res = vj_strndup( str, 255 );   
    }   
    return res;
}

int vj_event_parse_msg( void *ptr, char *msg, int msg_len )
{
    veejay_t *v = (veejay_t*)ptr;
    int net_id = 0;
    int np = 0;
    
    if( msg == NULL )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Dropped empty VIMS message");
        return 0;
    }

    if( msg_len < MSG_MIN_LEN )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Dropped VIMS message that was too small (%s)",msg);
        return 0;

    }
    
    if( strncasecmp( msg, "BUN", 3 ) == 0 )
    {
        veejay_chomp_str( msg, &msg_len );
        vj_event_parse_bundle( v, msg );
        return 1;
    }

    if( strncasecmp( msg, "KEY", 3 ) == 0 )
    {
        vj_event_parse_kf( v, msg, msg_len );
        return 1;
    }

    veejay_chomp_str( msg, &msg_len );
    msg_len --;

    /* try to scan VIMS id */
    if ( sscanf( msg, "%03d", &net_id ) != 1 )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Error parsing VIMS selector");
        return 0;
    }
    
    if( net_id <= 0 || net_id >= VIMS_MAX )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "VIMS Selector %d invalid", net_id );
        return 0;
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
    
	int i_args[MAX_VIMS_ARGUMENTS];

    veejay_memset(i_args, 0, sizeof(i_args) );

    np = vj_event_vevo_get_num_args( net_id );
        
    if ( msg_len <= MSG_MIN_LEN )
    {
        int i = 0;
        while(  i < np  )
        {
            i_args[i] = vj_event_vevo_get_default_value( net_id, i );
            i++;
        }
        vj_event_fire_net_event( v, net_id, NULL, i_args, np, 0 );
    }
    else
    {
        char *arguments = NULL;
        char *fmt = vj_event_vevo_get_event_format( net_id );
        int flags = vj_event_vevo_get_flags( net_id );
        int i = 0;
        char *str = NULL;
        int fmt_offset = 1;
        char *arg_str = NULL;
        int n = 4;
        if( msg[msg_len-4] == ';' )
            n = 5;

        arg_str = arguments = vj_strndup( msg + 4 , msg_len - n );

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
               arguments += 1;
        }

        i ++;

        if( flags & VIMS_ALLOW_ANY )
            i = np;

        vj_event_fire_net_event( v, net_id, str, i_args, i, 0 );

        if(fmt) free(fmt);
        if(arg_str) free(arg_str);
        if(str) free(str);

    }

	void *macro = NULL;
    int loop_stat_stop = 0;
	if( SAMPLE_PLAYING(v)) {
		macro = sample_get_macro(v->uc->sample_id);
        loop_stat_stop = sample_get_loop_stats(v->uc->sample_id);
	}
	if( STREAM_PLAYING(v)) {
		macro = vj_tag_get_macro(v->uc->sample_id);
        loop_stat_stop = vj_tag_get_loop_stats(v->uc->sample_id);
	}

	if( macro == NULL )
		return 0;

	if( vj_macro_get_status( macro ) == MACRO_REC ) {
   		if( vj_macro_is_vims_accepted(net_id)) {
			int at_dup = 0;
			int at_loop = 0;
			vj_event_macro_get_loop_dup(v, &at_dup, &at_loop );
			if(vj_macro_put( macro, msg, v->settings->current_frame_num, at_dup, at_loop ) == 0 ) {
				veejay_msg(0, "[Macro] max number of VIMS messages for this position reached");
			} else {
                vj_macro_set_loop_stat_stop( macro, loop_stat_stop );
            }
		}
	}

    return 0;
}

void vj_event_update_remote(void *ptr)
{
    veejay_t *v = (veejay_t*)ptr;
    int i;

    int p1 = vj_server_poll( v->vjs[VEEJAY_PORT_CMD] );
    int p2 = vj_server_poll( v->vjs[VEEJAY_PORT_STA] );
    int p3 = vj_server_poll( v->vjs[VEEJAY_PORT_DAT] );

    int has_n=0;

    if( p1  )
    has_n += vj_server_new_connection(v->vjs[VEEJAY_PORT_CMD]);
    
    if( p2 )
    has_n += vj_server_new_connection( v->vjs[VEEJAY_PORT_STA] );
    
    if( p3 )
    has_n += vj_server_new_connection( v->vjs[VEEJAY_PORT_DAT] );

    if( v->settings->use_vims_mcast )
    {
        int res = vj_server_poll( v->vjs[VEEJAY_PORT_MAT] );
        if( res > 0 ) {
             res = vj_server_update(v->vjs[VEEJAY_PORT_MAT],0 );
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
    }

    v->settings->is_dat = 0;
    for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
    {   
        if( vj_server_link_can_read( v->vjs[VEEJAY_PORT_CMD], i ) )
        {
            vj_server_init_msg_pool( v->vjs[VEEJAY_PORT_CMD], i ); // ensure pool is ready

            int res = vj_server_update( v->vjs[VEEJAY_PORT_CMD], i );
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
            }else if( res <= 0 ) 
            {
                _vj_server_del_client( v->vjs[VEEJAY_PORT_CMD], i );
                _vj_server_del_client( v->vjs[VEEJAY_PORT_STA], i );
            }
        }
    }

    v->settings->is_dat = 1;
    for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
    {   
        if( vj_server_link_can_read( v->vjs[VEEJAY_PORT_DAT], i ) )
        {
            vj_server_init_msg_pool( v->vjs[VEEJAY_PORT_DAT], i ); // ensure pool is ready
                
            int res = vj_server_update( v->vjs[VEEJAY_PORT_DAT], i );
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
            } else if( res <= 0 )   
            {
                _vj_server_del_client( v->vjs[VEEJAY_PORT_DAT], i );
            }
        }
    }

    v->settings->is_dat = 0;

	void *macro = NULL;

	if(SAMPLE_PLAYING(v)) {
		macro = sample_get_macro(v->uc->sample_id);
	} else if (STREAM_PLAYING(v)) {
		macro = vj_tag_get_macro(v->uc->sample_id);
	}

	if(macro == NULL) {
		return;
	}

   	if(vj_macro_get_status(macro) == MACRO_PLAY)
    {
       	int at_dup = 0;
		int at_loop = 0;
		vj_event_macro_get_loop_dup(v, &at_dup, &at_loop );

		char key[32];
		vj_macro_get_key(v->settings->current_frame_num, at_dup, at_loop , key, sizeof(key));

		char **vims_messages = vj_macro_play_event( macro, key );

		if(vims_messages != NULL) {
			for( i = 0; vims_messages[i] != NULL; i ++ ) {
				vj_event_parse_msg(v,vims_messages[i], strlen(vims_messages[i]));
			}
			free(vims_messages);
		}

		vj_macro_finish_event( macro ,key );
	}
}

void    vj_event_commit_bundle( veejay_t *v, int key_num, int key_mod)
{
    char bundle[4096];
    vj_event_create_effect_bundle(v, bundle, key_num, key_mod );
}

#ifdef HAVE_SDL
int vj_event_single_fire(void *ptr , SDL_Event event, int pressed)
{
    SDL_KeyboardEvent *key = &event.key;
    veejay_t *v =  (veejay_t*) ptr;
    int vims_mod = 0;
 	vj_keyboard_event *ev = NULL;

    if( event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ) {	
		SDL_Keymod mod = key->keysym.mod;
    
    	if( (mod & KMOD_LSHIFT) || (mod & KMOD_RSHIFT )) // could use direct KMOD_SHIFT ?
			vims_mod |= VIMS_MOD_SHIFT;
		if( (mod & KMOD_LALT) || (mod & KMOD_ALT) ) // ONLY LEFT SHIFT !!!
			vims_mod |= VIMS_MOD_ALT;
    	if( (mod & KMOD_CTRL) || (mod & KMOD_CTRL) ) // Both CTRL (but not explicit l & r)
			vims_mod |= VIMS_MOD_CTRL;
		if( (mod & KMOD_CAPS) ) {
   	    	vims_mod = VIMS_MOD_CAPSLOCK; // FIXME change to |= or not ???
	    }

    	int vims_key = key->keysym.scancode;
    	int index = vims_mod * SDL_NUM_SCANCODES + vims_key;

    	ev = get_keyboard_event( index );

    	veejay_msg(VEEJAY_MSG_DEBUG,
            "VIMS modifier: %d (SDL modifier %d/%x), Key %d, VIMS event %p",
                vims_mod, mod,mod, vims_key, ev );
    }

    if(!ev )
    {
        if(event.type == SDL_MOUSEWHEEL && event.wheel.y >0 && v->use_osd != 3 ) {
            char msg[100];
            sprintf(msg,"%03d:;", VIMS_VIDEO_SKIP_SECOND );
            vj_event_parse_msg( (veejay_t*) ptr, msg, strlen(msg) );
            return 1;
        } else if (event.type == SDL_MOUSEWHEEL && event.wheel.y < 0 && v->use_osd != 3) {
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
            veejay_msg(VEEJAY_MSG_DEBUG, "Requested VIMS Bundle %d does not exist", event_id);
            return 0;
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

void vj_event_none(void *ptr, const char format[], va_list ap)
{
    veejay_msg(VEEJAY_MSG_DEBUG, "No action implemented for requested event");
}

#ifdef HAVE_XML2
static  void    get_cstr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *what, char *dst, size_t dst_len )
{
    char *t = NULL;
    if(! xmlStrcmp( cur->name, what ))
    {
        t = get_xml_str( doc, cur );
        veejay_strncpy( dst, t, dst_len );  
        free(t);
    }
}

static  void    get_fstr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *what, float *dst )
{
    if(!xmlStrcmp( cur->name, what ))
    {
        *dst = get_xml_float( doc, cur );
    }
}

static  void    get_istr( xmlDocPtr doc, xmlNodePtr cur, const xmlChar *what, int *dst )
{
    if(! xmlStrcmp( cur->name, what ))
    {
        *dst = get_xml_int( doc, cur );
    }
}

#define XML_CONFIG_STREAM       "stream"
#define XML_CONFIG_STREAM_SOURCE    "source"
#define XML_CONFIG_STREAM_FILENAME  "filename"
#define XML_CONFIG_STREAM_TYPE      "type"
#define XML_CONFIG_STREAM_COLOR     "rgb"
#define XML_CONFIG_STREAM_OPTION    "option"
#define XML_CONFIG_STREAM_CHAIN     "fxchain"

#define XML_CONFIG_KEY_SYM "key_symbol"
#define XML_CONFIG_KEY_MOD "key_modifier"
#define XML_CONFIG_KEY_VIMS "vims_id"
#define XML_CONFIG_KEY_EXTRA "extra"
#define XML_CONFIG_EVENT "event"
#define XML_CONFIG_FILE "config"
#define XML_CONFIG_SETTINGS    "run_settings"
#define XML_CONFIG_SETTING_PORTNUM "port_num"
#define XML_CONFIG_SETTING_HOSTNAME "hostname"
#define XML_CONFIG_SETTING_PRIOUTPUT "primary_output"
#define XML_CONFIG_SETTING_PRINAME   "primary_output_destination"
#define XML_CONFIG_SETTING_SDLSIZEX   "SDLwidth"
#define XML_CONFIG_SETTING_SDLSIZEY   "SDLheight"
#define XML_CONFIG_SETTING_AUDIO     "audio"
#define XML_CONFIG_SETTING_SYNC      "sync"
#define XML_CONFIG_SETTING_TIMER     "timer"
#define XML_CONFIG_SETTING_FPS       "output_fps"
#define XML_CONFIG_SETTING_GEOX      "Xgeom_x"
#define XML_CONFIG_SETTING_GEOY      "Xgeom_y"
#define XML_CONFIG_SETTING_BEZERK    "bezerk"
#define XML_CONFIG_SETTING_COLOR     "nocolor"
#define XML_CONFIG_SETTING_YCBCR     "chrominance_level"
#define XML_CONFIG_SETTING_WIDTH     "output_width"
#define XML_CONFIG_SETTING_HEIGHT    "output_height"
#define XML_CONFIG_SETTING_DFPS      "dummy_fps"
#define XML_CONFIG_SETTING_DUMMY    "dummy"
#define XML_CONFIG_SETTING_NORM      "video_norm"
#define XML_CONFIG_SETTING_MCASTOSC  "mcast_osc"
#define XML_CONFIG_SETTING_MCASTVIMS "mcast_vims"
#define XML_CONFIG_SETTING_SCALE     "output_scaler"    
#define XML_CONFIG_SETTING_PMODE    "play_mode"
#define XML_CONFIG_SETTING_PID      "play_id"
#define XML_CONFIG_SETTING_SAMPLELIST "sample_list"
#define XML_CONFIG_SETTING_FILEASSAMPLE "file_as_sample"
#define XML_CONFIG_SETTING_EDITLIST   "edit_list"
#define XML_CONFIG_BACKFX         "backfx"
#define XML_CONFIG_COMPOSITEMODE    "composite_mode"
#define XML_CONFIG_SCALERFLAGS      "scaler_flags"
#define XML_CONFIG_SETTING_OSD      "use_osd"

static void vj_event_format_xml_settings( veejay_t *v, xmlNodePtr node  )
{
    put_xml_int( node, XML_CONFIG_SETTING_PRIOUTPUT, v->video_out );
    put_xml_int( node, XML_CONFIG_SETTING_SDLSIZEX, v->bes_width );
    put_xml_int( node, XML_CONFIG_SETTING_SDLSIZEY, v->bes_height );
    put_xml_int( node, XML_CONFIG_SETTING_GEOX, v->uc->geox );
    put_xml_int( node, XML_CONFIG_SETTING_GEOY, v->uc->geoy );
    put_xml_int( node, XML_CONFIG_SETTING_WIDTH, v->video_output_width );
    put_xml_int( node, XML_CONFIG_SETTING_HEIGHT, v->video_output_height );
    put_xml_int( node, XML_CONFIG_SETTING_AUDIO, v->audio );
    put_xml_int( node, XML_CONFIG_SETTING_SYNC, v->sync_correction );
    put_xml_int( node, XML_CONFIG_SETTING_TIMER, v->uc->use_timer );
    put_xml_int( node, XML_CONFIG_SETTING_BEZERK, v->no_bezerk );
    put_xml_int( node, XML_CONFIG_SETTING_COLOR, veejay_is_colored() );
    put_xml_int( node, XML_CONFIG_SETTING_YCBCR, v->pixel_format );
    put_xml_float( node, XML_CONFIG_SETTING_DFPS, v->dummy->fps );
    put_xml_int( node, XML_CONFIG_SETTING_NORM, v->dummy->norm );
    put_xml_int( node, XML_CONFIG_SETTING_DUMMY, v->dummy->active );
    put_xml_int( node, XML_CONFIG_SETTING_MCASTOSC, v->settings->use_mcast );
    put_xml_int( node, XML_CONFIG_SETTING_MCASTVIMS, v->settings->use_vims_mcast );
    put_xml_int( node, XML_CONFIG_SETTING_SCALE, v->settings->zoom );
    put_xml_float( node, XML_CONFIG_SETTING_FPS, v->settings->output_fps );
    put_xml_int( node, XML_CONFIG_SETTING_PMODE, v->uc->playback_mode );
    put_xml_int( node, XML_CONFIG_SETTING_PID, v->uc->sample_id );
    put_xml_int( node, XML_CONFIG_BACKFX, v->settings->fxdepth );
    put_xml_int( node, XML_CONFIG_COMPOSITEMODE, v->settings->composite );
    put_xml_int( node, XML_CONFIG_SCALERFLAGS, v->settings->sws_templ.flags );
    put_xml_int( node, XML_CONFIG_SETTING_FILEASSAMPLE, v->uc->file_as_sample );
    put_xml_int( node, XML_CONFIG_SETTING_OSD, v->use_osd );
}

static void vj_event_xml_parse_config( veejay_t *v, xmlDocPtr doc, xmlNodePtr cur )
{
    if( veejay_get_state(v) != LAVPLAY_STATE_STOP)
        return;

    int c = 0;
    char sample_list[1024];
    // FIXME editlist loading ; veejay restart

    while( cur != NULL )
    {
        get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_SAMPLELIST, sample_list,sizeof(sample_list) );
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
//      get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_SETTING_NORM, &(v->dummy->norm) );
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
    v->settings->action_scheduler.sl = vj_strdup( sample_list );
    veejay_msg(VEEJAY_MSG_DEBUG, "Scheduled '%s' for restart", sample_list );
        
    v->settings->action_scheduler.state = 1;
}

void vj_event_xml_new_keyb_event( void *ptr, xmlDocPtr doc, xmlNodePtr cur )
{
    int key = 0;
    int key_mod = 0;
    int event_id = 0;   
    
    char msg[4096];
    veejay_memset(msg,0,sizeof(msg));

    while( cur != NULL )
    {
        get_istr( doc, cur, (const xmlChar*) XML_CONFIG_KEY_VIMS, &event_id );
        get_cstr( doc, cur, (const xmlChar*) XML_CONFIG_KEY_EXTRA, msg,sizeof(msg) );
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
        int b_key = key, b_mod = key_mod;
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
                     "VIMS Bundle %d already exists. Ignoring VIMS Bundle defined in action file", event_id);
                return;
            }
            else
            {
                if(vj_event_bundle_del(event_id) != 0)
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "Unable to delete VIMS bundle %d", event_id);
                    return;
                }
            }
        }

        vj_msg_bundle *m = vj_event_bundle_new( msg, event_id);
        if(!m)
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to create a new VIMS Bundle %d - [%s]", event_id, msg );
            return;
        }
    
        m->accelerator = b_key;
        m->modifier    = b_mod;


        if(vj_event_bundle_store(m))
            veejay_msg(VEEJAY_MSG_DEBUG, "Added VIMS Bundle %d , trigger with key %d using modifier %d", event_id, b_key, b_mod);
    }

#ifdef HAVE_SDL
    if( key > 0 && key_mod >= 0)
    {
        if( override_keyboard )
            vj_event_unregister_keyb_event( key, key_mod );
        if( !vj_event_register_keyb_event( event_id, key, key_mod, NULL ))
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to attach key %d with modifier %d to VIMS Bundle %d", key,key_mod,event_id);
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
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to open file '%s' for reading", file_name);   
        return 0;
    }
    
    cur = xmlDocGetRootElement( doc );
    if( cur == NULL)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "The file '%s' is not in the expected format", file_name);
        xmlFreeDoc(doc);
        return 0;
    }

    if( xmlStrcmp( cur->name, (const xmlChar *) XML_CONFIG_FILE))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "The file '%s' is not a Veejay Action File", file_name );
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

void    vj_event_format_xml_event( xmlNodePtr node, int event_id )
{
    int key_id=0;
    int key_mod=-1;

    if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END)
    {
            /* its a Bundle !*/
        vj_msg_bundle *m = vj_event_bundle_get( event_id );
        if(!m) 
        {   
            veejay_msg(VEEJAY_MSG_ERROR, "VIMS Bundle %d does not exist", event_id);
            return;
        }
    
        put_xml_str( node, XML_CONFIG_KEY_EXTRA, m->bundle );
    }
    /* Put all known VIMS so we can detect differences in runtime
           some Events will not exist if SDL, Jack, DV, Video4Linux would be missing */

    put_xml_int( node, XML_CONFIG_KEY_VIMS, event_id );
        
#ifdef HAVE_SDL
    if(key_id > 0 && key_mod >= 0 )
    {
        put_xml_int( node, XML_CONFIG_KEY_SYM, key_id );
        put_xml_int( node, XML_CONFIG_KEY_MOD, key_mod );
    }
#endif
}

void vj_event_write_actionfile(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    char file_name[512];
    int args[2] = {0,0};
    int i;
    //veejay_t *v = (veejay_t*) ptr;
    xmlDocPtr doc;
    xmlNodePtr rootnode,childnode;  
    P_A(args,sizeof(args),file_name,sizeof(file_name),format,ap);
    doc = xmlNewDoc( (const xmlChar*) "1.0" );
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
void    vj_event_read_file( void *ptr,  const char format[], va_list ap )
{
    char file_name[512];
    int args[1];

	P_A(args,sizeof(args),file_name,sizeof(file_name),format,ap);

#ifdef HAVE_XML2
    if(veejay_load_action_file( ptr, file_name ))
        veejay_msg(VEEJAY_MSG_INFO, "Loaded Action file '%s'", file_name );
    else
        veejay_msg(VEEJAY_MSG_ERROR,"Unable to load Action file '%s'", file_name );
#endif
}

#ifdef HAVE_SDL
vims_key_list   *vj_event_get_keys( int event_id )
{
    vims_key_list *list = vj_calloc( sizeof(vims_key_list));
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
#ifdef ARCH_X86_64
    uint64_t eid = (uint64_t) event_id;
#else
    uint32_t eid = (uint32_t) event_id;
#endif    
    hnode_t *node = hash_lookup(keyboard_eventid_map, (void*) eid);
    if(node == NULL) {
		return list;
	}

    vj_keyboard_event *ev = hnode_get( node );
    if(ev)
    {
        list->key_symbol = ev->key_symbol;
        list->key_mod = ev->key_mod;
        list->args = ev->arguments;
        list->arg_len = ev->arg_len;
    }

    return list;
}

void    vj_event_unregister_keyb_event( int sdl_key, int modifier )
{
    int index = (modifier * SDL_NUM_SCANCODES) + sdl_key;
    vj_keyboard_event *ev = get_keyboard_event( index );
    if(ev)
    {
        vj_msg_bundle *m = vj_event_bundle_get( ev->event_id );
        if(m) 
        {
            m->accelerator = 0;
            m->modifier = 0;

            vj_event_bundle_update( m, ev->event_id );
        }

        veejay_msg(VEEJAY_MSG_DEBUG, "Dropped key binding for VIMS Bundle %d", ev->event_id);
        
        if( ev->vims )
            free(ev->vims);
        if( ev->arguments)
            free(ev->arguments );

        veejay_memset(ev, 0, sizeof( vj_keyboard_event ));

        del_keyboard_event( index );
    }
}

static int vj_event_update_key_collection(vj_keyboard_event *ev, int index)
{
    hnode_t *node = hnode_create( ev );
    hnode_t *node2 = hnode_create( ev );
    if(!node || !node2)
    {
	    veejay_msg(0,"Unable to create a new node");
        return 0;
    }

#ifdef ARCH_X86_64
    uint64_t tid = (uint64_t) index;
    uint64_t eid = (uint64_t) ev->event_id;
#else
    uint32_t tid = (uint32_t) index;
    uint32_t eid = (uint32_t) ev->event_id;
#endif

    hnode_t *old = hash_lookup( keyboard_events, (void*) tid );
    if(old) {
        hash_delete( keyboard_events, old );
    }

    old = hash_lookup( keyboard_eventid_map, (void*) eid );
    if(old) {
        hash_delete( keyboard_eventid_map, old );
    }

    //register keybinding by index (SDLK key mne)
    hash_insert( keyboard_events, node, (void*) tid );

    //register keybinding by event id
    hash_insert( keyboard_eventid_map, node2, (void*) eid );

    return 1;
}

int     vj_event_register_keyb_event(int event_id, int symbol, int modifier, char *value)
{
    int offset = SDL_NUM_SCANCODES * modifier;
    int index = offset + symbol;
    int is_bundle = (event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END);

    vj_keyboard_event *ff = get_keyboard_event(index);
    if( ff == NULL ) {
        char *extra = value;
        /* registering a new bundle or vims event triggered by key action */
        if(is_bundle) {
            char val[10];
            sprintf(val, "%d", event_id);
            extra = val;
        }   
        ff = new_keyboard_event( symbol, modifier, extra, event_id ); 
        if( ff == NULL ) {
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to register a new action");
            return 0;
        }
    }
    else {
        /* the action already exists, free old stuff */
        if(ff->arguments) {
        	free(ff->arguments);
	    }
    }

    if( is_bundle ) {
        vj_msg_bundle *bun = vj_event_bundle_get( event_id );
        bun->accelerator = symbol;
        bun->modifier = modifier;
        vj_event_bundle_update( bun, event_id );
    }

    configure_vims_key_event( ff,symbol,modifier, event_id, value );

	return vj_event_update_key_collection(ff, index);
}
#endif
static void vj_event_init_network_events()
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
                result = vj_strdup( vj_event_default_sdl_keys[i].value );
            break;
        }
    }
    return result;
}

static void vj_event_load_keyboard_configuration(veejay_t *info)
{
    char path[1024];
    snprintf(path,sizeof(path), "%s/keyboard.cfg", info->homedir);
    FILE *f = fopen( path,"r" );
    if(!f) {
        veejay_msg(VEEJAY_MSG_WARNING,"No user defined keyboard configuration in %s", path );
        return;
    }

    char msg[100];
    int event_id = 0;
    int key = 0;
    int mod = 0;
    int keyb_events = 0;
    while( (fscanf( f, "%d,%d,%d,\"%[^\"]\"", &event_id,&key,&mod,msg ) ) == 4 ) {
        if( vj_event_register_keyb_event(
                event_id,
                key,
                mod,
                msg ) ) {
            keyb_events++;
        } else {
            veejay_msg(VEEJAY_MSG_ERROR,"Error registering VIMS event %03d", event_id );
        }
    }

    if( keyb_events > 0 )
        veejay_msg(VEEJAY_MSG_INFO,"Loaded %d keyboard events from %s", keyb_events, path );

    fclose(f);
}

static void vj_event_init_keyboard_defaults()
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
            veejay_msg(VEEJAY_MSG_ERROR, "VIMS event %03d does not exist ", vj_event_default_sdl_keys[i].event_id );
        }
    }
}
#endif

void vj_event_init(void *ptr)
{
    veejay_t *v = (veejay_t*) ptr;
    int i;
#ifdef HAVE_SDL 
    veejay_memset( keyboard_event_map_, 0, sizeof(keyboard_event_map_));
#endif
    vj_init_vevo_events();
#ifdef HAVE_SDL
    if( !(keyboard_events = hash_create( MAX_KEY_MNE, int_bundle_compare, int_bundle_hash)))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize hash for keyboard events");
        return;
    }
    if( !(keyboard_eventid_map = hash_create( MAX_KEY_MNE, int_bundle_compare, int_bundle_hash))) {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot creating mapping between keyboard events and VIMS event identifiers");
        return;
    }
#endif
    for(i=0; i < VIMS_MAX; i++)
    {
        net_list[i].act = vj_event_none;
        net_list[i].list_id = 0;
    }
#ifdef HAVE_SDL
    if( !(BundleHash = hash_create(MAX_KEY_MNE, int_bundle_compare, int_bundle_hash)))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot initialize hashtable for message bundles");
        return;
    }
#endif

    vj_event_init_network_events();
#ifdef HAVE_SDL
    vj_event_init_keyboard_defaults();
    if(ptr) vj_event_load_keyboard_configuration(v);
#endif
	vj_macro_init();
}

#ifdef HAVE_SDL
static  void vj_event_destroy_hash( hash_t *h)
{
    if(!hash_isempty(h)) {
        hscan_t s = (hscan_t) {0};
        hash_scan_begin( &s, h );
        hnode_t *node = NULL;
        while((node = hash_scan_next(&s)) != NULL ) {
            void *ptr = hnode_get(node);
            if(ptr) destroy_keyboard_event(ptr);
        }
        hash_free_nodes( h );
    }
    hash_destroy( h );
}
#endif

static void vj_event_destroy_bundles( hash_t *h )
{
    if(!hash_isempty(h)) {
        hscan_t s = (hscan_t) {0};
        hash_scan_begin( &s, h );
        hnode_t *node = NULL;
        while((node = hash_scan_next(&s)) != NULL ) {
            vj_msg_bundle *m = hnode_get(node);
            if(m) {
                if( m->bundle )
                  free(m->bundle);
                free(m);    
            }   
        }
        hash_free_nodes( h );
    }
    hash_destroy( h );
}

void    vj_event_destroy(void *ptr)
{
#ifdef HAVE_SDL
	//let's not destroy keyboard mappings, we are shutting down anyway so why bother
#endif
    if(BundleHash) vj_event_destroy_bundles( BundleHash );

    if(sample_image_buffer)
        free(sample_image_buffer);

    vj_picture_free();  

    vj_event_vevo_free();

}

void vj_event_linkclose(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    veejay_msg(VEEJAY_MSG_INFO, "Remote requested session-end");
    int i = v->uc->current_link;
    if( v->vjs[0] )
        _vj_server_del_client( v->vjs[0], i );
    if( v->vjs[1] )
        _vj_server_del_client( v->vjs[1], i );
    if( v->vjs[2] )
        _vj_server_del_client( v->vjs[3], i );
}

void vj_event_quit(void *ptr, const char format[], va_list ap)
{
    int i;
    veejay_t *v = (veejay_t*)ptr;
    veejay_msg(VEEJAY_MSG_INFO, "Remote requested veejay quit, quitting Veejay");
//@ hang up clients
    for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ )
    {   
        if( vj_server_link_used( v->vjs[VEEJAY_PORT_CMD], i ) ) 
        {
            _vj_server_del_client(v->vjs[VEEJAY_PORT_CMD],i);
            _vj_server_del_client(v->vjs[VEEJAY_PORT_STA],i);
            _vj_server_del_client(v->vjs[VEEJAY_PORT_DAT],i);
        }
    }

    veejay_quit(v);
}

void  vj_event_sample_mode(void *ptr,   const char format[],    va_list ap)
{
}

void    vj_event_set_framerate( void *ptr, const char format[] , va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
        int args[2];
		P_A(args,sizeof(args),NULL,0,format,ap);

    float new_fps = (float) args[0] * 0.01;

    if(new_fps == 0.0 )
        new_fps = v->current_edit_list->video_fps;
    else if (new_fps <= 0.25 ) {
        new_fps = 0.25f;
        veejay_msg(VEEJAY_MSG_WARNING, "Limited new framerate to %2.2f ", new_fps );
    }

    if( v->audio == AUDIO_PLAY ) {
        veejay_msg(VEEJAY_MSG_WARNING,"You need to run without audio playback(-a0) to dynamically change the framerate");
    }else {
        veejay_set_framerate( v, new_fps );
        veejay_msg(VEEJAY_MSG_INFO, "Playback engine is now playing at %2.2f FPS", new_fps );
    }
}

void    vj_event_sync_correction( void *ptr,const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];

	P_A(args,sizeof(args),NULL,0,format,ap);

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
        int n = sample_highest();
        vj_el_break_cache( v->edit_list );
        for( i = 1; i <= n; i ++ ) {
            editlist *e = sample_get_editlist(i);
            if(e) {
                vj_el_break_cache(e); k++;
            }
        }
        veejay_msg(VEEJAY_MSG_INFO,"Cleared %d samples from cache", k );
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
    if( veejay_get_state(v) == LAVPLAY_STATE_PAUSED )
    {
        veejay_change_state(v, LAVPLAY_STATE_PLAYING );
        veejay_msg(VEEJAY_MSG_WARNING, "Resume playback");
    }
    else {
        veejay_change_state(v, LAVPLAY_STATE_PAUSED);
        veejay_msg(VEEJAY_MSG_WARNING, "Suspend playback");
    }
}

void    vj_event_play_norestart( void *ptr, const char format[], va_list ap )
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    //@ change mode so veejay does not restart samples at all

    if( args[0] == 0 ) {    
        //@ off
        v->settings->sample_restart = 0;
    } else if ( args[0] == 1 ) {
        //@ on
        v->settings->sample_restart = 1;
    }

    veejay_msg(VEEJAY_MSG_INFO, "Sample continuous mode is %s", (v->settings->sample_restart == 0 ? "enabled" : "disabled"));
}

void    vj_event_sub_render_entry( void *ptr, const char format[], va_list ap )
{
    int args[3];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if( SAMPLE_PLAYING(v)) {
        SAMPLE_DEFAULTS(args[0]);

        if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);

        sample_entry_set_is_rendering(args[0],args[1], args[2]);

        int subrender = sample_get_subrender(args[0],args[1],&args[2]);

        veejay_msg(VEEJAY_MSG_INFO, "%s rendering of mixing source on entry %d",
                ( (subrender == 1 ? args[2] : 0) ? "Enabled" : "Disabled" ),args[1]);
    } 
    if( STREAM_PLAYING(v)) {
        STREAM_DEFAULTS(args[0]);
        
        if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);

        vj_tag_entry_set_is_rendering(args[0],args[1],args[2]);

        int subrender = vj_tag_get_subrender(args[0],args[1],&args[2]);

        veejay_msg(VEEJAY_MSG_INFO, "%s rendering of mixing source on entry %d",
                ( (subrender == 1 ? args[2] :0) ? "Enabled" : "Disabled" ),args[1]);
    }
}   


void    vj_event_sub_render( void *ptr, const char format[], va_list ap )
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if( SAMPLE_PLAYING(v)) {
        SAMPLE_DEFAULTS(args[0]);
        int ignored = 0;
        int cur = sample_get_subrender(args[0],0,&ignored);
        if( cur == 0 ) {
            cur = 1;
        }
        else {
            cur = 0;
        }

        sample_set_subrender(args[0], cur);
        cur = sample_get_subrender(args[0],0,&ignored);

        veejay_msg(VEEJAY_MSG_INFO, "%s rendering of mixing sources",
                ( cur == 1 ? "Enabled" : "Disabled" ));
    } 
    if( STREAM_PLAYING(v)) {
        STREAM_DEFAULTS(args[0]);
        int ignored = 0;
        int cur = vj_tag_get_subrender(args[0],0,&ignored);
        if( cur == 0 ) {
            cur = 1;
        }
        else {
            cur = 0;
        }

        vj_tag_set_subrender(args[0], cur);
        cur = vj_tag_get_subrender(args[0],0,&ignored);
        veejay_msg(VEEJAY_MSG_INFO, "%s rendering of mixing sources",
                ( cur == 1 ? "Enabled" : "Disabled" ));
    }
}   

void vj_event_set_play_mode_go(void *ptr, const char format[], va_list ap) 
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;

    P_A(args,sizeof(args),NULL,0,format,ap);
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
            SAMPLE_DEFAULTS(args[1]);
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
            STREAM_DEFAULTS(args[1]);
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

void    vj_event_set_rgb_parameter_type(void *ptr, const char format[], va_list ap)
{   
    int args[3];
    P_A(args,sizeof(args),NULL,0,format,ap);
    if(args[0] >= 0 && args[0] <= 3 )
    {
        vje_set_rgb_parameter_conversion_type( args[0] );
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
    veejay_msg(VEEJAY_MSG_INFO, "Next frame will be taken for static background");
}

void    vj_event_send_keylist( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    unsigned int len=0;
    char message[256];
    char *blob = NULL;
    char line[512];
    char header[7];
    int skip = 0;
#ifdef HAVE_SDL
    blob = vj_calloc( 1024 * 64 );

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
                    snprintf( line, 512, "%04d%03d%03d%03zu%s",
                        ev->event_id, ev->key_mod, ev->key_symbol, strlen(message), message );
                    int line_len = strlen(line);
                    len += line_len;
                    veejay_strncat( blob, line, line_len);
                }
                skip = 0;
            }   
        }
    }
#endif
    sprintf( header, "%06d", len );

    SEND_MSG( v, header );
    if( blob != NULL )
        SEND_MSG( v, blob );

    free( blob );

}

void    vj_event_send_bundles(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    vj_msg_bundle *m;
    int i;
    char tmp[4096];
    char *buf = vj_calloc( 65535 );

    for( i = 0; i <= 600 ; i ++ )
    {
        if( i >= VIMS_BUNDLE_START && i < VIMS_BUNDLE_END )
        {
            if(!vj_event_bundle_exists(i))
                continue;

            m = vj_event_bundle_get(i);

            int bun_len = strlen(m->bundle);
            if( bun_len <= 0 )
                continue;

            snprintf(tmp,sizeof(tmp)-1,"%04d%03d%03d%04d%s%03d%03d",
                i, m->accelerator, m->modifier, bun_len, m->bundle, 0,0 );

            veejay_strncat( buf, tmp, strlen(tmp) );
        }
        else
        {
            if( !vj_event_exists(i) || (i >= 400 && i < VIMS_BUNDLE_START) )
                continue;
        
            char *name = vj_event_vevo_get_event_name(i);
            char *form  = vj_event_vevo_get_event_format(i);
            int name_len = strlen(name);
            int form_len = (form ? strlen(form)  : 0);
#ifdef HAVE_SDL
            vims_key_list *tree = vj_event_get_keys( i );
            while( tree != NULL )
            {
                snprintf(tmp, sizeof(tmp)-1, "%04d%03d%03d%04d%s%03d%03d",
                    i, tree->key_symbol, tree->key_mod, name_len, name, form_len, tree->arg_len );

                veejay_strncat( buf,tmp,strlen(tmp));

                if(form)
                    veejay_strncat( buf, form, form_len);   
                if(tree->arg_len)
                    veejay_strncat( buf, tree->args, tree->arg_len );
                
                void *tree_ptr = tree;

                tree = tree->next;

                free(tree_ptr);
            }
#endif
            if(name)
                free(name);

            if(form)
                free(form);
        }
    }

    int  pack_len = strlen( buf );
    char header[7];
    
    snprintf(header, sizeof(header),"%06d", pack_len );
    
    SEND_MSG(v, header);
    SEND_MSG(v,buf);

    free(buf);
}

void    vj_event_send_vimslist(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    char *buf = vj_event_vevo_list_serialize();
    SEND_MSG(v,buf);
    if(buf) free(buf);
}

void    vj_event_send_devicelist( void *ptr, const char format[], va_list ap)
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
    P_A( args,sizeof(args), NULL ,0, format, ap);

    SAMPLE_DEFAULTS(args[0]);
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
    P_A( args, 1, s ,0, format, ap);
    STREAM_DEFAULTS(args[0]);
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

    int last_tag = vj_tag_highest_valid_id();
    int last_sample=sample_highest_valid_id();

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
        if(sample_size() <= 0)
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

void    vj_event_set_volume(void *ptr, const char format[], va_list ap)
{
    int args[1];    
    P_A(args,sizeof(args),NULL,0,format,ap);

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
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(vj_event_valid_mode(args[0]))
    {
        int mode = args[0];
        /* check if current playing ID is valid for this mode */
        if(mode == VJ_PLAYBACK_MODE_SAMPLE)
        {
            int last_id = sample_highest_valid_id();
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
            int last_id = vj_tag_highest_valid_id();
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
    veejay_t *v = (veejay_t*) ptr;
    if(PLAIN_PLAYING(v) || SAMPLE_PLAYING(v)) 
    {
        int args[2];
        P_A(args,sizeof(args),NULL,0,format,ap);

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
                veejay_msg(VEEJAY_MSG_ERROR, "Cannot copy EDL");
                return;
            }
            int start = 0;
            int end   = el->total_frames;

            sample_info *skel = sample_skeleton_new(start, end );
            if(skel)
            {
                skel->edit_list = el;
            	if(sample_store(skel)==0) 
            	{
               		veejay_msg(VEEJAY_MSG_INFO, "Created new sample [%d]", skel->sample_id);
            	}
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

}

void    vj_event_fullscreen(void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(args[0] == 2) { // Its a toggle!
        args[0] = (v->settings->full_screen ? 0 : 1);
    }

    switch(v->video_out)
    {
        case 0:
        case 2:
#ifdef HAVE_SDL
        {
            char *caption = veejay_title(v);

            void *tmpsdl = vj_sdl_allocate( v->effect_frame1,v->use_keyb,v->use_mouse,v->show_cursor, v->borderless );
           
            int x = -1, y = -1;
            vj_sdl_get_position( v->sdl, &x, &y );

            if(vj_sdl_init(
                tmpsdl,
                x,
                y,
                v->bes_width,
                v->bes_height,
                caption,
                1,
                args[0],
                v->pixel_format,
                v->current_edit_list->video_fps
            ) ) {
                if( v->sdl ) {
                    vj_sdl_free(v->sdl);
                }
                v->sdl = tmpsdl;        
                if(args[0])
                    vj_sdl_grab( v->sdl, 0 );
                v->settings->full_screen = args[0];
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
    P_A(args,sizeof(args),NULL,0,format,ap);

    int w  = args[0];
    int h  = args[1];
    int x  = args[2];
    int y  = args[3];

    if( w < 0 || w > 4096 || h < 0 || h > 4096)
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
                if( v->sdl) {
                    vj_sdl_free(v->sdl);
                    vj_sdl_quit();
                    v->sdl = NULL;
                    veejay_msg(VEEJAY_MSG_INFO, "Closed SDL window");
                }
                v->video_out = 5;
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
                if(!v->sdl) {
                    v->sdl = vj_sdl_allocate( v->effect_frame1, v->use_keyb, v->use_mouse, v->show_cursor, v->borderless );
                    veejay_msg(VEEJAY_MSG_INFO, "Allocated SDL window");
                
                    if(vj_sdl_init(
                        v->sdl,
                        x,
                        y,
                        w,
                        h,
                        title,
                        1,
                        v->settings->full_screen,
                        v->pixel_format,
                        v->current_edit_list->video_fps )
                    ) {
                        veejay_msg(VEEJAY_MSG_INFO, "Opened SDL Video Window of size %d x %d", w, h );
                        v->video_out = 0;
                        v->bes_width = w;
                        v->bes_height = h;
                    }
                }
#endif
            case 0:
#ifdef HAVE_SDL  
                if(v->sdl) {
                    vj_sdl_free(v->sdl);
                    vj_sdl_quit();
                    v->sdl = NULL;
                }           
                if(!v->sdl) {
                    v->sdl = vj_sdl_allocate( v->effect_frame1, v->use_keyb, v->use_mouse, v->show_cursor, v->borderless );
                    veejay_msg(VEEJAY_MSG_INFO, "Allocated SDL window");
                
                    if(vj_sdl_init(
                        v->sdl,
                        x,
                        y,
                        w,
                        h,
                        title,
                        1,
                        v->settings->full_screen,
                        v->pixel_format,
                        v->current_edit_list->video_fps )
                    ) {
                        veejay_msg(VEEJAY_MSG_INFO, "Opened SDL Video Window of size %d x %d", w, h );
                        v->video_out = 0;
                        v->bes_width = w;
                        v->bes_height = h;
                    }
                }
#endif              
                break;
            default:
                break;
        }
        free(title);
    }
}

void    vj_event_promote_me( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    vj_server_client_promote( v->vjs[VEEJAY_PORT_CMD], v->uc->current_link );
    v->rmodes[ v->uc->current_link ] = -1000;
    veejay_msg(VEEJAY_MSG_DEBUG, "Promoted link %d", v->uc->current_link ); 
}

void vj_event_play_stop(void *ptr, const char format[], va_list ap) 
{
    veejay_t *v = (veejay_t*) ptr;
    if(!STREAM_PLAYING(v))
    {
        int speed = v->settings->current_playback_speed;
        if(speed != 0)
        {
            v->settings->previous_playback_speed = speed;
            veejay_set_speed(v, 0 );
            veejay_msg(VEEJAY_MSG_INFO,"Video is paused");
        }
        else
        {
            veejay_set_speed(v, v->settings->previous_playback_speed );
            veejay_msg(VEEJAY_MSG_INFO,"Video is playing (resumed at speed %d)", v->settings->previous_playback_speed);
        }
    }
    else
    {
        p_invalid_mode();
    }
}

void vj_event_play_stop_all(void *ptr, const char format[], va_list ap) 
{
    veejay_t *v = (veejay_t*) ptr;
      

    if(STREAM_PLAYING(v))
    {
	vj_tag_set_chain_paused( v->uc->sample_id, v->settings->current_playback_speed == 0 ? 0 : 1 );
    }
    else if(SAMPLE_PLAYING(v)) {
	int speed = v->settings->current_playback_speed;
        if(speed != 0)
        {
            v->settings->previous_playback_speed = speed;
            veejay_set_speed(v, 0 );
	    
	    sample_set_chain_paused( v->uc->sample_id, 1);

            veejay_msg(VEEJAY_MSG_INFO,"Video is paused");
        }
        else
        {
            veejay_set_speed(v, v->settings->previous_playback_speed );
	    
	    sample_set_chain_paused( v->uc->sample_id, 0);

            veejay_msg(VEEJAY_MSG_INFO,"Video is playing (resumed at speed %d)", v->settings->previous_playback_speed);
        }

    }
    else
    {
        p_invalid_mode();
    }
}

void vj_event_feedback( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
    P_A(args,sizeof(args),NULL,0,format,ap);
    if( args[0] == 0 ) {
        v->settings->feedback = 0;
    } else if ( args[0] == 1 ) {
        v->settings->feedback = 1;
        v->settings->feedback_stage = 1;
    }
    veejay_msg(VEEJAY_MSG_INFO ,"Feedback on main source is %s",
            ( v->settings->feedback ? "enabled" : "disabled") );
}

void    vj_event_render_depth( void *ptr, const char format[] , va_list ap )
{
    int args[1];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);
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
        veejay_msg(VEEJAY_MSG_INFO, "Rendering FX chain on mixing inputs");
    } else {
        veejay_msg(VEEJAY_MSG_INFO, "Not rendering FX chain on mixing inputs");
    }
}

void    vj_event_viewport_composition( void *ptr, const char format[], va_list ap )
{
}

void vj_event_play_reverse(void *ptr,const char format[],va_list ap) 
{
    veejay_t *v = (veejay_t*)ptr;
    if(!STREAM_PLAYING(v))
    {
        int speed = v->settings->current_playback_speed;
        if( speed == 0 ) {
            speed = -1; 
        }
        else if(speed > 0) {
            speed = -(speed);
        } 
        veejay_set_speed(v,speed );

        veejay_msg(VEEJAY_MSG_INFO, "Video is playing in reverse at speed %d", speed);
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

        veejay_set_speed(v,speed );  

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
        int speed = 0;
        P_A(args,sizeof(args),NULL,0,format,ap);
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

void    vj_event_hold_frame( void *ptr, const char format[], va_list ap )
{
    int args[3];
    veejay_t *v = (veejay_t*) ptr;

    if(SAMPLE_PLAYING(v)||PLAIN_PLAYING(v)) {
        P_A( args,sizeof(args),NULL,0,format, ap);
        if(args[1] <= 0 )
            args[1] = 1;
        if(args[2] <= 0 )
            args[2] = 1;
        if(args[2] >= 300)
            args[2] = 1;
        veejay_hold_frame(v,args[1],args[2]);
    } else {
        p_invalid_mode();
    }
}

void vj_event_play_speed_kb(void *ptr, const char format[], va_list ap)
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    if(!STREAM_PLAYING(v))
    {
        P_A(args,sizeof(args),NULL,0,format,ap);
    
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
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    if(PLAIN_PLAYING(v) || SAMPLE_PLAYING(v))
    {
        if(args[0] <= 0 )
            args[0] = 1;

        if(veejay_set_framedup(v, args[0]))
        {
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
        P_A(args,sizeof(args),NULL,0,format,ap);
        if(args[0] == -1 )
            args[0] = v->current_edit_list->total_frames;
        veejay_set_frame(v, args[0]);
    }
    else
    {
        p_invalid_mode();
    }
}

void vj_event_set_frame_percentage(void *ptr, const char format[], va_list ap)
{
    int args[1];
    veejay_t *v = (veejay_t*) ptr;
    if(!STREAM_PLAYING(v))
    {
        P_A(args,sizeof(args),NULL,0,format,ap);
        
        int len = sample_get_endFrame(v->uc->sample_id) - sample_get_startFrame(v->uc->sample_id);
        float p = fabs( ( (float)args[0] * 0.01f)); // absolute values
        int perc = (int) ((len * p) + 0.5f); // round to nearest integer
        veejay_set_frame(v, perc);
    }
    else
    {
        p_invalid_mode();
    }
}

void    vj_event_projection_dec( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);
    
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
void    vj_event_projection_inc( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);
    
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
    P_A( args,sizeof(args),NULL,0,format, ap );
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
    P_A( args,sizeof(args),NULL,0,format, ap );
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
    P_A( args,sizeof(args),NULL,0,format, ap );
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
    P_A( args,sizeof(args),NULL,0,format, ap );
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
        
        if( v->uc->sample_end < v->uc->sample_start) {
            int se = v->uc->sample_end;
            v->uc->sample_end = v->uc->sample_start;
            v->uc->sample_start = se;   
            veejay_msg(VEEJAY_MSG_WARNING, "Swapped starting and ending positions: %ld - %ld, please set a new starting position", v->uc->sample_start,v->uc->sample_end );
        }
        
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
                veejay_msg(VEEJAY_MSG_ERROR, "Unable to clone current editlist");
                return;
            }

            long start = 0;
            long end   = el->total_frames;
    
            sample_info *skel = sample_skeleton_new(start,end);
            if(!skel)
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Unable to create a new sample");
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
                veejay_msg(VEEJAY_MSG_ERROR,"Unable to create a new sample");
            }
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

void    vj_event_sample_rand_start( void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    video_playback_setup *settings = v->settings;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(args[0] == RANDTIMER_FRAME)
        settings->randplayer.timer = RANDTIMER_FRAME;
    else
        settings->randplayer.timer = RANDTIMER_LENGTH;


    settings->randplayer.mode = RANDMODE_SAMPLE;

    vj_perform_randomize(v);
    veejay_msg(VEEJAY_MSG_INFO, "Started sample randomizer, %s",
            (settings->randplayer.timer == RANDTIMER_FRAME ? "freestyling" : "playing full length of gambled samples"));    
}

void    vj_event_sample_rand_stop( void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    video_playback_setup *settings = v->settings;

    if(settings->randplayer.mode != RANDMODE_INACTIVE)
        veejay_msg(VEEJAY_MSG_INFO, "Stopped sample randomizer");
    else
        veejay_msg(VEEJAY_MSG_ERROR, "Sample randomizer not started");
    settings->randplayer.mode = RANDMODE_INACTIVE;
}

void vj_event_sample_set_rand_loop(void *ptr, const char format[], va_list ap)
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    SAMPLE_DEFAULTS(args[0]);

    if( args[1] == -1 )
    {
        if( sample_exists(args[0]) )
        {
            int cur_loop = sample_get_looptype(args[0]);
            if( cur_loop == 4 ) {
                sample_set_looptype(args[0],3 );
            }
            else {
                sample_set_looptype(args[0],4 );
            }
            cur_loop = sample_get_looptype(args[0]);

            veejay_msg(VEEJAY_MSG_INFO,"Sample %d loop type is now %s", args[0], (cur_loop == 3 ? "Random" : "Play once and keep playing last frame" ) );
        }
        else
        {   
            p_no_sample(args[0]);
        }

    }
}

void vj_event_sample_set_loop_type(void *ptr, const char format[], va_list ap)
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    SAMPLE_DEFAULTS(args[0]);

	if(!sample_exists(args[0])) {
		p_no_sample(args[0]);
		return;
	}

	if(args[1] == -1) {
        int lp = sample_get_looptype(args[0]);
		if( lp == 1 && args[1] == -1 )
			lp = 2;
		else if ( lp >= 2 && args[1] == -1 )
			lp = 1;

		sample_set_looptype( args[0], lp );

        veejay_msg(VEEJAY_MSG_INFO, "Sample %d loop type is now %s",args[0],
                    ( lp==1 ? "Normal Looping" : lp==2 ? "Pingpong Looping" : "No Looping" ) );

    }
	else if(args[1] >= 0 && args[1] <= 4)
    {
        int lp = sample_set_looptype( args[0] , args[1]);
        lp = sample_get_looptype(args[0]);
        switch(lp)
        {
        	case 0: veejay_msg(VEEJAY_MSG_INFO, "Play once");break;
            case 1: veejay_msg(VEEJAY_MSG_INFO, "Normal looping");break;
            case 2: veejay_msg(VEEJAY_MSG_INFO, "Pingpong looping");break;
            case 3: veejay_msg(VEEJAY_MSG_INFO, "Random frame");break;
            case 4: veejay_msg(VEEJAY_MSG_INFO, "Play once (no pause)");break;
        }
    }
}

void    vj_event_sample_set_position( void *ptr, const char format[], va_list ap )
{
    int args[3];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0, format, ap);

    SAMPLE_DEFAULTS(args[0]);

    int entry = args[1];
    if( entry == -1 )
        entry = sample_get_selected_entry(args[0]);

    sample_get_chain_source(args[0], entry);
    int cha = sample_get_chain_channel( args[0], entry );

    int pos = sample_get_offset( cha,entry );
            
    pos += args[2];

    sample_set_offset( cha,entry, pos );

    veejay_msg(VEEJAY_MSG_INFO, "Changed frame position to %d for sample %d on FX entry %d (only)", pos,cha,entry );
}

void    vj_event_sample_skip_frame(void *ptr, const char format[], va_list ap)
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format, ap);

    SAMPLE_DEFAULTS(args[0]);
    
    int job = sample_highest();
    int i   = 1;
    int k   = 0;
    for( i = 1; i <= job; i ++ ) {
        sample_info *si = sample_get( i );
        if(!si)
          continue;
        
        //@ find the mixing ID in all effect chains , frame offset is FX chain attribute
        for( k = 0; k < SAMPLE_MAX_EFFECTS; k ++ ) {
            if( si->effect_chain[k]->effect_id > 0 && // active
                    si->effect_chain[k]->source_type == 0 &&     // sample (=0)
                si->effect_chain[k]->channel == args[0] ) { // ID
                //@ vars needed for range check
                int start = sample_get_startFrame(
                        si->effect_chain[k]->channel );
                int end   = sample_get_endFrame(
                        si->effect_chain[k]->channel );
                int len   = end - start;

                //@ skip frame = increment current with offset in args[1]
                si->effect_chain[k]->frame_offset += args[1];
                
                //@ check range
                if( si->effect_chain[k]->frame_offset > len )
                    si->effect_chain[k]->frame_offset = len;
                if( si->effect_chain[k]->frame_offset < 0 )
                    si->effect_chain[k]->frame_offset = 0;
                
                veejay_msg(VEEJAY_MSG_DEBUG,
                    "Set offset of mixing sample #%d (%d-%d) on chain entry %d of sample %d to %d",
                        si->effect_chain[k]->channel,start,end, k,i, si->effect_chain[k]->frame_offset );   
            }
        }
    }
}

void vj_event_sample_set_speed(void *ptr, const char format[], va_list ap)
{
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0, format, ap);

    SAMPLE_DEFAULTS(args[0]);

    if( sample_set_speed(args[0], args[1]) != -1)
    {
        veejay_msg(VEEJAY_MSG_INFO, "Changed speed of sample %d to %d",args[0],args[1]);
    }
    else
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Speed %d it too high to set on sample %d",
            args[1],args[0]); 
    }
}

void vj_event_set_transition(void *ptr, const char format[], va_list ap)
{
    int args[5];
    veejay_t *v = (veejay_t*) ptr;
    P_A( args, sizeof(args), NULL, 0, format, ap );

    int playmode = args[0];
    int sample_id = args[1];

    if(playmode != VJ_PLAYBACK_MODE_SAMPLE && playmode != VJ_PLAYBACK_MODE_TAG) {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid playback (%d) mode on setting transition", playmode);
        return;
    }

    if(playmode == VJ_PLAYBACK_MODE_SAMPLE) {
        SAMPLE_DEFAULTS(sample_id);
    }
    else {
        STREAM_DEFAULTS(sample_id);
    }

    int transition_active = args[2];
    int transition_shape = args[3];
    int transition_length = args[4];

    if( playmode == VJ_PLAYBACK_MODE_SAMPLE ) {
        if(transition_length > 0)
            sample_set_transition_length( sample_id, transition_length );

        sample_set_transition_shape( sample_id, transition_shape );
        sample_set_transition_active( sample_id, transition_active );
    }

    if( playmode == VJ_PLAYBACK_MODE_TAG ) {
        if(transition_length > 0)
            vj_tag_set_transition_length( sample_id, transition_length );
        vj_tag_set_transition_shape( sample_id, transition_shape );
        vj_tag_set_transition_active( sample_id, transition_active );
    }


    veejay_msg(VEEJAY_MSG_DEBUG,"%s %d set transition active %d, shape %d, length %d",
            (playmode == VJ_PLAYBACK_MODE_SAMPLE ? "Sample"  : "Stream"),
            sample_id, transition_active, transition_shape, transition_length );
}

void vj_event_sample_set_marker_start(void *ptr, const char format[], va_list ap) 
{
    int args[2];
    veejay_t *v = (veejay_t*)ptr;
    
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    SAMPLE_DEFAULTS(args[0]);

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
    
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    SAMPLE_DEFAULTS(args[0]);

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
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    SAMPLE_DEFAULTS(args[0]);

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
    P_A(args,sizeof(args),NULL,0,format,ap);

    SAMPLE_DEFAULTS(args[0]);

    if( sample_exists(args[0]) )
    {
        if( sample_marker_clear( args[0] ) )
        {
            veejay_msg(VEEJAY_MSG_INFO, "Sample %d marker cleared", args[0]);
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
    P_A(args,sizeof(args),NULL,0,format,ap);

    SAMPLE_DEFAULTS(args[0]);

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
    }
    else
    {
        p_no_sample(args[0]);
    }
}

void vj_event_mixing_sample_set_speed(void *ptr, const char format[], va_list ap)
{
    int args[1];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format, ap);

	if(SAMPLE_PLAYING(v)) {
		int entry= sample_get_selected_entry( v->uc->sample_id );
		int type = sample_get_chain_source( v->uc->sample_id, entry );
		if(type == 0) {
			int sample_id = sample_get_chain_channel( v->uc->sample_id, entry );
			sample_set_speed( sample_id, args[0] );
			veejay_msg(VEEJAY_MSG_INFO, "Changed speed of mixing sample %d to %d on entry %d",sample_id,args[0], entry);
		}
	}
	if(STREAM_PLAYING(v)) {
		int entry = vj_tag_get_selected_entry( v->uc->sample_id );
		int type = vj_tag_get_chain_source( v->uc->sample_id, entry );
		if( type == 0 ) {
			int sample_id = vj_tag_get_chain_channel( v->uc->sample_id, entry );
			sample_set_speed( sample_id, args[0] );
			veejay_msg(VEEJAY_MSG_INFO, "Changed speed of mixing sample %d to %d on entry %d",sample_id,args[0], entry);
		}
	}
}

void vj_event_mixing_sample_set_dup(void *ptr, const char format[], va_list ap)
{
    int args[1];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),NULL,0,format, ap);

	if(SAMPLE_PLAYING(v)) {
		int entry= sample_get_selected_entry( v->uc->sample_id );
		int type = sample_get_chain_source( v->uc->sample_id, entry );
		if(type == 0) {
			int sample_id = sample_get_chain_channel( v->uc->sample_id, entry );
			sample_set_framedup( sample_id, args[0] );
			veejay_msg(VEEJAY_MSG_INFO, "Changed frame duplication of mixing sample %d to %d on entry %d",sample_id,args[0], entry);
		}
	}
	if(STREAM_PLAYING(v)) {
		int entry = vj_tag_get_selected_entry( v->uc->sample_id );
		int type = vj_tag_get_chain_source( v->uc->sample_id, entry );
		if( type == 0 ) {
			int sample_id = vj_tag_get_chain_channel( v->uc->sample_id, entry );
			sample_set_framedup( sample_id, args[0] );
			veejay_msg(VEEJAY_MSG_INFO, "Changed frame duplication of mixing sample %d to %d on entry %d",sample_id,args[0], entry);
		}
	}
}

void    vj_event_tag_set_descr( void *ptr, const char format[], va_list ap)
{
    char str[TAG_MAX_DESCR_LEN];
    int args[2];
    veejay_t *v = (veejay_t*) ptr;
    P_A(args,sizeof(args),str,sizeof(str),format,ap);

    if(!STREAM_PLAYING(v))
    {
        p_invalid_mode();
        return;
    }

    STREAM_DEFAULTS(args[0]);

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
    P_A(args,sizeof(args),str,sizeof(str),format,ap);

    SAMPLE_DEFAULTS(args[0]);

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
    P_A(NULL,0,str,sizeof(str),format,ap);
    if(sample_writeToFile( str, v->composite,v->seq,v->font, v->uc->sample_id, v->uc->playback_mode) )
    {
        veejay_msg(VEEJAY_MSG_INFO, "Saved %d samples to file '%s'", sample_size(), str);
    }
    else
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Error saving samples to file %s", str);
    }
}

void vj_event_sample_load_list(void *ptr, const char format[], va_list ap)
{
    char str[1024];
    veejay_t *v = (veejay_t*) ptr;
    P_A( NULL,0, str,sizeof(str), format, ap);

    int id = 0;
    int mode = 0;
    
    if( sample_readFromFile( str, v->composite,v->seq, v->font, v->edit_list,&id, &mode ) ) 
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
    char prefix[150];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if( !SAMPLE_PLAYING(v))
    {
        if(!STREAM_PLAYING(v) && !v->seq->active) {
            p_invalid_mode();
            return;
        }
    }
    else if( !STREAM_PLAYING(v))
    {
        if(!SAMPLE_PLAYING(v) && !v->seq->active) {
            p_invalid_mode();
            return;
        }
    }
    
    int format_ = _recorder_format;
    if(format_==-1)
    {
        veejay_msg(VEEJAY_MSG_ERROR,"Select a video codec first");
        return; 
    }

    if( !v->seq->active )
    {
        sample_get_description(v->uc->sample_id, prefix );
    }
    else
    {
        if( v->seq->rec_id )
        {
            veejay_msg(0, "Already recording the sequence");
            return;
        }
        else
        {
            sprintf( prefix, "sequence_");
        }
    }

    if(!veejay_create_temp_file(prefix, tmp))
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create temporary file, Record aborted" );
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
                if( v->seq->samples[i].type == 0)
                    args[0] += sample_get_frame_length( v->seq->samples[i].sample_id );
                else
                    args[0] += vj_tag_get_n_frames( v->seq->samples[i].sample_id );
            }

	    	veejay_sample_set_initial_positions( v );
	    	v->seq->current = 0;
        }
        veejay_msg(VEEJAY_MSG_DEBUG, "\tRecording %d frames (sequencer is %s)", args[0],
                (v->seq->active ? "active" : "inactive"));
    }
    
    if(args[0] <= 1 )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Cowardly refusing to record less then 2 frames");
        return;
    }

    //FIXME refactor in veejaycoresample
    if( sample_init_encoder( v->uc->sample_id, tmp, format_, v->effect_frame1, v->current_edit_list, args[0]) == 1)
    {
        video_playback_setup *s = v->settings;
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
        if( STREAM_PLAYING(v) && !vj_tag_exists(v->uc->sample_id) )
            veejay_msg(VEEJAY_MSG_ERROR,"issue #60: You need a sample which identifier matches that of the current playing stream... ");

        veejay_msg(VEEJAY_MSG_ERROR,"Unable to start sample recorder");
        sample_stop_encoder( v->uc->sample_id );
        result = 0;
        v->settings->sample_record = 0;
		v->seq->rec_id = 0;
        return;
    }   

    if(result == 1)
    {
        v->settings->sample_record = 1;
        v->settings->sample_record_switch = args[1];
    }

    if( v->seq->active )
    {
        v->seq->rec_id = v->uc->sample_id;
    }
    else
    {
        veejay_set_frame(v, sample_get_resume(v->uc->sample_id));
    }
}

void vj_event_sample_rec_stop(void *ptr, const char format[], va_list ap) 
{
    char avi_file[1024];
    veejay_t *v = (veejay_t*)ptr;
    
    if( SAMPLE_PLAYING(v)) 
    {
        video_playback_setup *s = v->settings;
        int stop_sample = v->uc->sample_id;

        if(v->seq->rec_id )
            stop_sample = v->seq->rec_id;

        if( sample_stop_encoder( stop_sample ) == 1 ) 
        {
            v->settings->sample_record = 0;
			v->seq->rec_id = 0;
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
                    veejay_msg(VEEJAY_MSG_ERROR, "Unable to append file %s to EDL",avi_file);
            
        
                sample_reset_encoder( stop_sample );
                s->sample_record = 0;   
                s->sample_record_id = 0;
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

void vj_event_sample_rel_start(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t *)ptr;
    int args[4];
    int s_start;
    int s_end;

    P_A(args,sizeof(args),NULL,0,format,ap);
    SAMPLE_DEFAULTS(args[0]);

    if(!sample_exists(args[0]))
    {
        p_no_sample(args[0]);
        return;
    }

    s_start = sample_get_startFrame(args[0]) + args[1];
    s_end = sample_get_endFrame(args[0]) + args[2];

    if  (sample_set_startframe(args[0],s_start) &&
        sample_set_endframe(args[0],s_end))
    {
        constrain_sample( v, args[0] );
        veejay_msg(VEEJAY_MSG_INFO, "Sample update start %d end %d",
            s_start,s_end);
    }
}

void vj_event_sample_set_start(void *ptr, const char format[], va_list ap) 
{
    veejay_t *v = (veejay_t *)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    SAMPLE_DEFAULTS(args[0]);

    if(!sample_exists(args[0]))
    {
        p_no_sample(args[0]);
        return;
    }

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
        veejay_msg(VEEJAY_MSG_ERROR, "Sample %d 's starting position %d must be greater than ending position %d",
            args[0],args[1], sample_get_endFrame(args[0]));
    }
}

void vj_event_sample_set_end(void *ptr, const char format[] , va_list ap)
{
    veejay_t *v = (veejay_t *)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    SAMPLE_DEFAULTS(args[0]);

    if(!sample_exists(args[0]))
    {
        p_no_sample(args[0]);
        return;
    }

    if(args[1] == -1)
        args[1] = sample_video_length( args[0] );
    
    if(args[1] <= 0 )
    {
        veejay_msg(0, "Impossible to set ending position %d for sample %d", args[1],args[0] );
        return;
    }
    if( args[1] >= sample_get_startFrame(args[0]))
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
    P_A(args,sizeof(args),NULL,0,format,ap);
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
            if( v->seq->samples[i].sample_id == deleted_sample && v->seq->samples[i].type == 0 )
                v->seq->samples[i].sample_id = 0;

        sample_verify_delete( args[0] , 0 );
    }
    else
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to delete sample %d",args[0]);
    }
}

void vj_event_sample_copy(void *ptr, const char format[] , va_list ap)
{
    int args[1];
    int new_sample =0;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if( sample_exists(args[0] ))
    {
        new_sample = sample_copy(args[0]);
        if(!new_sample)
            veejay_msg(VEEJAY_MSG_ERROR, "Failed to copy sample %d",args[0]);
    }
}

void vj_event_sample_clear_all(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    if( SAMPLE_PLAYING(v)) {
        if( !vj_has_video(v,v->edit_list) ) {
            veejay_msg(VEEJAY_MSG_WARNING,"There are no video frames in plain mode");
            if( vj_tag_highest_valid_id() > 0 ) {
                veejay_msg(VEEJAY_MSG_WARNING,"Switching to stream 1 to clear all samples");
                veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_TAG, 1 );
            } else {
                veejay_msg(VEEJAY_MSG_ERROR, "Nothing to fallback to, cannot delete all samples");
                return;
            }
        } else {
            veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_PLAIN, 0 );
        }
    }

    sample_del_all(v->edit_list);
    vj_font_set_dict( v->font, NULL );

    veejay_memset(v->seq->samples, 0, sizeof(int) * MAX_SEQUENCES );
    v->seq->active = 0;
    v->seq->size = 0;

    veejay_msg(VEEJAY_MSG_INFO, "Deleted all samples");
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
        } else {
            p_invalid_mode();
            return;
        }
    }
    veejay_msg(VEEJAY_MSG_INFO, "Enabled effect chain");
}

void    vj_event_stream_set_length( void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(STREAM_PLAYING(v))
    {
        if(args[0] > 0 && args[0] < 2160000 ) //fictious length is maximum 1 day
        {
            vj_tag_set_n_frames(v->uc->sample_id, args[0]);
            v->settings->max_frame_num = args[0];
            constrain_stream( v, v->uc->sample_id, (long) args[0]);
        }
        else
          veejay_msg(VEEJAY_MSG_ERROR, "Ficticious length must be 0 - 2160000");
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
        veejay_msg(VEEJAY_MSG_INFO, "Effect chain on sample %d is disabled",v->uc->sample_id);
    }
    else
    {
        if(STREAM_PLAYING(v) )
        {
            vj_tag_set_effect_status(v->uc->sample_id, 0);
            veejay_msg(VEEJAY_MSG_INFO, "Effect chain on stream %d is enabled",v->uc->sample_id);
        }
        else
            p_invalid_mode();
    }
}

void vj_event_sample_chain_enable(void *ptr, const char format[], va_list ap) 
{
    veejay_t *v = (veejay_t*)ptr;
    int args[4];
    P_A(args,sizeof(args),NULL,0,format,ap);

    SAMPLE_DEFAULTS(args[0]);

    if(sample_exists(args[0]))
    {
        sample_set_effect_status(args[0], 1);
        veejay_msg(VEEJAY_MSG_INFO, "Effect chain on Sample %d is enabled",args[0]);
    }
}

void    vj_event_all_samples_chain_toggle(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
    P_A(args,sizeof(args),NULL,0,format,ap);
    if(SAMPLE_PLAYING(v))
    {
        int i;
        int n = sample_highest_valid_id();
        for(i=1; i <= n; i++)
            sample_set_effect_status( i, args[0] );
        veejay_msg(VEEJAY_MSG_INFO, "Effect Chain on all samples %s", (args[0]==0 ? "Disabled" : "Enabled"));
    }
    else
    {
        if(STREAM_PLAYING(v))
        {
            int i;  
            int n = vj_tag_highest_valid_id();
            for(i=1; i <= n; i++)
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
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(!STREAM_PLAYING(v))
    {
        p_invalid_mode();   
        return;
    }

    STREAM_DEFAULTS(args[0]);   

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
    P_A(args,sizeof(args),NULL,0,format,ap);

    STREAM_DEFAULTS(args[0]);

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
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(args[0] == 0)
    {
        args[0] = v->uc->sample_id;
    }
    
    if(SAMPLE_PLAYING(v) && sample_exists(args[0]))
    {
        sample_set_effect_status(args[0], 0);
        veejay_msg(VEEJAY_MSG_INFO, "Effect chain on sample %d is disabled",args[0]);
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
        veejay_msg(VEEJAY_MSG_INFO, "Effect chain is %s", (sample_get_effect_status(v->uc->sample_id) ? "enabled" : "disabled"));
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
        veejay_msg(VEEJAY_MSG_INFO, "Effect chain is %s", (vj_tag_get_effect_status(v->uc->sample_id) ? "enabled" : "disabled"));
    }
}

void vj_event_chain_entry_video_toggle(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v))
    {
        SAMPLE_DEFAULTS(args[0]);
        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
            int status = !sample_get_chain_status(args[0],args[1]);
            if(sample_set_chain_status(args[0],args[1], status)!=-1)
            {
                veejay_msg(VEEJAY_MSG_INFO, "Sample %d: Video on chain entry %d is %s",args[0],args[1],
                    status == 1 ? "Enabled" : "Disabled" );
            }
        }
        else
            p_no_sample(args[0]);
    }
    if(STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0]))
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
            int status = !vj_tag_get_chain_status(args[0],args[1]);
            if(vj_tag_set_chain_status(args[0],args[1],status)!=-1)
            {
                veejay_msg(VEEJAY_MSG_INFO, "Stream %d: Video on chain entry %d is %s",args[0],args[1],
                    status == 1 ? "Enabled" : "Disabled" );
            }
        }
        else
            p_no_tag(args[0]);
    }
}

void enable_chain_entry_video(void *ptr, const char format[], va_list ap, int enable_chain)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args), NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);
        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);

            if(sample_set_chain_status(args[0],args[1],enable_chain) != -1)
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
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0]))
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);

            if(vj_tag_set_chain_status(args[0],args[1],enable_chain)!=-1)
            {
                veejay_msg(VEEJAY_MSG_INFO, "Stream %d: Video on chain entry %d is %s",args[0],args[1],
                    vj_tag_get_chain_status(args[0],args[1]) == 1 ? "Enabled" : "Disabled" );
            }
        }
        else
            p_no_tag(args[0]);
    }
}

void vj_event_chain_entry_enable_video(void *ptr, const char format[], va_list ap)
{
    enable_chain_entry_video(ptr, format, ap, 1);
    return;
}
void vj_event_chain_entry_disable_video(void *ptr, const char format[], va_list ap)
{
    enable_chain_entry_video(ptr, format, ap, 0);
    return;
}

void    vj_event_chain_fade_follow(void *ptr, const char format[], va_list ap )
{
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if( args[0] == 0 || args[0] == 1 ) {
        vj_perform_follow_fade( ptr, args[0] );
    }
}

void    vj_event_manual_chain_fade(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[3];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
    {
        args[0] = v->uc->sample_id;
    }

    if( args[1] < 0 || args[1] > 255)
    {
        veejay_msg(VEEJAY_MSG_DEBUG,"Invalid opacity range %d use [0-255]", args[1]);
        //clamp values
        if(args[1] < 0)
            args[1] = 0;
        if(args[1] > 255)
            args[1] = 255;
    }

    args[1] = args[1];

    if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
    {
        if( sample_set_manual_fader( args[0], args[1] ) )
        {
            veejay_msg(VEEJAY_MSG_DEBUG, "Set chain fader opacity %f",sample_get_fader_val( args[0]));
        }
    }
    if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
    {
        if( vj_tag_set_manual_fader( args[0], args[1] ) )
        {
            veejay_msg(VEEJAY_MSG_DEBUG, "Set chain fader opacity %f",vj_tag_get_fader_val(args[0]));
        }
    }
}

void    vj_event_chain_fade_alpha(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
	P_A(args,sizeof(args), NULL,0,format,ap);

    if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
    {
        args[0] = v->uc->sample_id;
    }

    if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
    {
        sample_set_fade_alpha( args[0], args[1] );
    }
    if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
    {
        vj_tag_set_fade_alpha( args[0], args[1] );
    }
}

void    vj_event_chain_fade_method(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
	P_A(args,sizeof(args),NULL,0,format,ap);

    if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
    {
        args[0] = v->uc->sample_id;
    }

    if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
    {
        sample_set_fade_method( args[0], args[1] );
    }
    if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
    {
        vj_tag_set_fade_method( args[0], args[1] );
    }
}

void    vj_event_chain_fade_entry(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
	P_A(args,sizeof(args),NULL,0,format,ap);


    if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
    {
        args[0] = v->uc->sample_id;
    }

    if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
    {
        sample_set_fade_entry( args[0], args[1] );
    }
    if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
    {
        vj_tag_set_fade_entry( args[0], args[1] );
    }
}

void vj_event_chain_fade_in(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
	P_A(args,sizeof(args), NULL,0,format,ap);

    if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
    {
        args[0] = v->uc->sample_id;
    }

    if( args[1] == 0 ) 
        args[1] = 1; //@forward

    if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
    {
        if( sample_set_fader_active( args[0], args[1],1 ) )
        {
            veejay_msg(VEEJAY_MSG_INFO, "Chain Fade In from sample to full effect chain in %d frames. Per frame %2.4f",
                args[1], sample_get_fader_inc(args[0]));
        }
    }
    if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
    {
        if( vj_tag_set_fader_active( args[0], args[1],1 ) )
        {
            veejay_msg(VEEJAY_MSG_INFO,"Chain Fade In from stream to full effect chain in %d frames. Per frame %2.4f",
                args[1], sample_get_fader_inc(args[0]));
        }
    }
}

void vj_event_chain_fade_out(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args), NULL,0,format,ap);

    if(args[0] == 0 && (SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) )
    {
        args[0] = v->uc->sample_id;
    }

    if( args[1] == 0 ) //@backward
        args[1] = -1;

    if( SAMPLE_PLAYING(v) && sample_exists(args[0])) 
    {
        if( sample_set_fader_active( args[0], args[1],-1 ) )
        {
            veejay_msg(VEEJAY_MSG_INFO, "Chain Fade Out from sample to full effect chain in %d frames. Per frame %2.2f",
                args[1], sample_get_fader_inc(args[0]));
        }
    }
    if (STREAM_PLAYING(v) && vj_tag_exists(args[0])) 
    {
        if( vj_tag_set_fader_active( args[0], args[1],-1 ) )
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
    P_A(args,sizeof(args),NULL,0,format,ap);

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
            if(vje_is_valid(effect))
            {
                sample_chain_remove(args[0],i);
                veejay_msg(VEEJAY_MSG_INFO,"Sample %d: Deleted effect %s from entry %d",
                    args[0],vje_get_description(effect), i);
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
            if(vje_is_valid(effect))
            {
                vj_tag_chain_remove(args[0],i);
                veejay_msg(VEEJAY_MSG_INFO,"Stream %d: Deleted effect %s from entry %d",    
                    args[0],vje_get_description(effect), i);
            }
        }
        v->uc->chain_changed = 1;
    }
}

void vj_event_chain_entry_del(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int effect = sample_get_effect_any(args[0],args[1]);
            if( vje_is_valid(effect)) 
            {
                sample_chain_remove(args[0],args[1]);
                v->uc->chain_changed = 1;
                veejay_msg(VEEJAY_MSG_INFO,"Sample %d: Deleted effect %s from entry %d",
                    args[0],vje_get_description(effect), args[1]);
            }
        }
    }

    if (STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);   

        if(vj_tag_exists(args[0]))      
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int effect = vj_tag_get_effect_any(args[0],args[1]);
            if(vje_is_valid(effect))
            {
                vj_tag_chain_remove(args[0],args[1]);
                v->uc->chain_changed = 1;
                veejay_msg(VEEJAY_MSG_INFO,"Stream %d: Deleted effect %s from entry %d",    
                    args[0],vje_get_description(effect), args[1]);
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
    int args[MAX_ARGUMENTS];

    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
    
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            if(sample_chain_add(args[0],args[1],args[2])) 
            {
                v->uc->chain_changed = 1;

                sample_set_chain_status( args[0],args[1], args[3] );
            }
            else
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Cannot set effect %d on sample %d chain %d",args[2],args[0],args[1]);
            }
        }
    }
    if( STREAM_PLAYING(v)) 
    {
        STREAM_DEFAULTS(args[0]);
        if(vj_tag_exists(args[0]))
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]); 

            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            if(vj_tag_set_effect(args[0],args[1], args[2]))
            {
                v->uc->chain_changed = 1;
            
                vj_tag_set_chain_status( args[0], args[1], args[3] );
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
	P_A(args,sizeof(args), NULL,0,format,ap);

    if( SAMPLE_PLAYING(v)  )
    {
        if(args[0] >= 0 && args[0] < SAMPLE_MAX_EFFECTS)
        {
            if( sample_set_selected_entry( v->uc->sample_id, args[0])) 
            {
            veejay_msg(VEEJAY_MSG_INFO,"Selected entry %d [%s]",
              sample_get_selected_entry(v->uc->sample_id), 
              vje_get_description( 
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
                vje_get_description( 
                    vj_tag_get_effect_any(v->uc->sample_id,vj_tag_get_selected_entry(v->uc->sample_id))));
            }
        }   
    }
}

void vj_event_entry_up(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
    P_A(args,sizeof(args),NULL,0,format,ap);
    if(SAMPLE_PLAYING(v) || STREAM_PLAYING(v))
    {
        int effect_id=-1;
        int c=-1;
        int flag=-1;

        if(SAMPLE_PLAYING(v))
        {
            c = sample_get_selected_entry(v->uc->sample_id) + args[0];
            if(c >= SAMPLE_MAX_EFFECTS) c = 0;
            sample_set_selected_entry( v->uc->sample_id, c);
            effect_id = sample_get_effect_any(v->uc->sample_id, c );
            flag = sample_get_chain_status(v->uc->sample_id,c);
        }
        if(STREAM_PLAYING(v))
        {
            c = vj_tag_get_selected_entry(v->uc->sample_id)+args[0];
            if( c>= SAMPLE_MAX_EFFECTS) c = 0;
            vj_tag_set_selected_entry(v->uc->sample_id,c);
            effect_id = vj_tag_get_effect_any(v->uc->sample_id,c);
            flag = vj_tag_get_chain_status(v->uc->sample_id,c);
        }

        veejay_msg(VEEJAY_MSG_INFO, "Entry %d has effect %s %s",
            c, vje_get_description(effect_id), (flag==0 ? "Disabled" : "Enabled"));

    }
}

void vj_event_entry_down(void *ptr, const char format[] ,va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
    P_A(args,sizeof(args),NULL,0,format,ap);
    if(SAMPLE_PLAYING(v) || STREAM_PLAYING(v)) 
    {
        int effect_id=-1;
        int c = -1;
        int flag=-1;
        
        if(SAMPLE_PLAYING(v))
        {
            c = sample_get_selected_entry( v->uc->sample_id ) - args[0];
            if(c < 0) c = SAMPLE_MAX_EFFECTS-1;
            sample_set_selected_entry( v->uc->sample_id, c);
            effect_id = sample_get_effect_any(v->uc->sample_id, c );
            flag = sample_get_chain_status(v->uc->sample_id,c);
        }
        if(STREAM_PLAYING(v))
        {
            c = vj_tag_get_selected_entry(v->uc->sample_id) - args[0];
            if(c<0) c= SAMPLE_MAX_EFFECTS-1;
            vj_tag_set_selected_entry(v->uc->sample_id,c);
            effect_id = vj_tag_get_effect_any(v->uc->sample_id,c);
            flag = vj_tag_get_chain_status(v->uc->sample_id,c);
        }
        veejay_msg(VEEJAY_MSG_INFO , "Entry %d has effect %s %s",
            c, vje_get_description(effect_id), (flag==0 ? "Disabled" : "Enabled"));
    }
}

void vj_event_chain_entry_set_narg_val(void *ptr,const char format[], va_list ap)
{
    int args[MAX_ARGUMENTS];
    char str[4096];
    int value = 0;
    veejay_t *v = (veejay_t*)ptr;

    veejay_memset(args,0,sizeof(int) * MAX_ARGUMENTS); 

    P_A(args,sizeof(args),str,sizeof(str),format,ap);    

    if( sscanf( str, "%d" , &value ) != 1 )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid value" );
        return;
    }

    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);

            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }
    
            int effect = sample_get_effect_any(args[0], args[1]);
            int num_p   = vje_get_num_params(effect);
            if( args[2] > num_p ) {
                args[2] = num_p;
            }

            float min = (float) vje_get_param_min_limit(effect, args[2]);
            float max = (float) vje_get_param_max_limit(effect, args[2]);

            float val = min + (max * ((float) value / 100.0f));

            if(sample_set_effect_arg(args[0],args[1],args[2],(int) val )==-1)   
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Error setting argument %d value %d for %s",args[2],(int)val,vje_get_description(effect));
            }
            v->uc->chain_changed = 1;
        }
    }
    else if( STREAM_PLAYING(v)) 
    {
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0])) 
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int effect = vj_tag_get_effect_any(args[0], args[1]);
            int num_p   = vje_get_num_params(effect);
            if( args[2] > num_p ) {
                args[2] = num_p;
            }

            float min = (float) vje_get_param_min_limit(effect, args[2]);
            float max = (float) vje_get_param_max_limit(effect, args[2]);

            float val = min + (max * ((float)value/100.0f));

            if(vj_tag_set_effect_arg(args[0],args[1],args[2],(int) val)==-1)
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Error setting argument %d value %d for %s",args[2],(int)val,vje_get_description(effect));
            }
            v->uc->chain_changed = 1;
        }
    }

}

void vj_event_chain_entry_preset(void *ptr,const char format[], va_list ap)
{
    long int tmp = 0;
    int base = 10;
    int index = 4; // sample, chain, fx_id, status
    int args[MAX_ARGUMENTS];
    char str[1024]; 
    char *end = str;
    veejay_t *v = (veejay_t*)ptr;
    veejay_memset(args,0,sizeof(int) * MAX_ARGUMENTS); 
   
    P_A(args,sizeof(args),str,sizeof(str),format,ap);

    while( (tmp = strtol( end, &end, base ))) {
        args[index] = (int) tmp;
        index ++;
    }

    if(SAMPLE_PLAYING(v)) 
    {
        int num_p = 0;  

        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of bounds: %d", args[1]);
                return;
            }

            int real_id = args[2];
            int i;
            num_p   = vje_get_num_params(real_id);
            
            if(sample_chain_add( args[0],args[1],args[2]))
            {
                int args_offset = 4;
                
                sample_set_chain_status( args[0],args[1], args[3] );

                for(i=0; i < num_p; i++)
                {
                    if(vje_is_param_value_valid(real_id,i,args[(i+args_offset)]) )
                    {
                        if(sample_set_effect_arg(args[0],args[1],i,args[(i+args_offset)] )==-1) 
                        {
                            veejay_msg(VEEJAY_MSG_ERROR, "Error setting argument %d value %d for %s",
                            i,
                            args[(i+args_offset)],
                            vje_get_description(real_id));
                        }
                    }
                }
            }
        }
    }
    if( STREAM_PLAYING(v)) 
    {
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0])) 
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of bounds %d", args[1]);
                return;
            }

            int real_id = args[2];
            int num_p   = vje_get_num_params(real_id);
            int i;
        
            if(vj_tag_set_effect(args[0],args[1], args[2]) )
            {
                int args_offset = 4;
            
                vj_tag_set_chain_status( args[0], args[1], args[3] );

                for(i=0; i < num_p; i++) 
                {
                    if(vje_is_param_value_valid(real_id, i, args[i+args_offset]) )
                    {
                        if(vj_tag_set_effect_arg(args[0],args[1],i,args[i+args_offset]))
                        {
                            veejay_msg(VEEJAY_MSG_DEBUG, "Changed parameter %d to %d (%s)",
                                i,
                                args[i+args_offset],
                                vje_get_description(real_id));
                        }
                    }
                }
                v->uc->chain_changed = 1;
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
                cha = vj_tag_highest_valid_id();
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
                cha = sample_highest_valid_id();
                if(cha<=0)
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
                    return;
                }
            }
            veejay_msg(VEEJAY_MSG_DEBUG, "Switched from source Stream to Sample");
            src = 0;
        }
        sample_set_chain_source(v->uc->sample_id,entry,src);
        sample_set_chain_channel(v->uc->sample_id,entry,cha);
        veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d", entry,(src==VJ_TAG_TYPE_NONE ? "Sample":"Stream"), cha);
        if(v->no_bezerk)
        {
            veejay_set_frame(v, sample_get_resume(v->uc->sample_id));
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
                cha = vj_tag_highest_valid_id();
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
                cha = sample_highest_valid_id();
                if(cha<=0)
                {
                    veejay_msg(VEEJAY_MSG_ERROR, "No samples to mix with");
                    return;
                }
            }
            src = 0;
        }
        vj_tag_set_chain_source(v->uc->sample_id,entry,src);
        vj_tag_set_chain_channel(v->uc->sample_id,entry,cha);

        vj_tag_get_descriptive(cha, description);
        veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d (%s)", entry,( src == 0 ? "Sample" : "Stream" ), cha,description);
    } 
}

void vj_event_chain_entry_source(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[3];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int src = args[2];
            int c = sample_get_chain_channel(args[0],args[1]);
            if(src == VJ_TAG_TYPE_NONE)
            {
                if(!sample_exists(c))
                {
                    c = sample_highest_valid_id();
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
                    c = vj_tag_highest_valid_id();
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
               int sample_offset = sample_get_offset(args[0],args[1]);
               int sample_speed = 0;
               if( src == VJ_TAG_TYPE_NONE )
                   sample_speed = sample_get_speed(c);
                veejay_msg(VEEJAY_MSG_INFO, "Mixing with source (%s %d) at speed %d position %d", 
                    src == VJ_TAG_TYPE_NONE ? "sample" : "stream",c,sample_speed,sample_offset);
                if(v->no_bezerk)
                {
                    veejay_set_frame(v,
                        sample_get_resume(args[0]));
                }
            }
                
        }
    }
    if(STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0]))
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int src = args[2];
            int c = vj_tag_get_chain_channel(args[0],args[1]);

            if(src == VJ_TAG_TYPE_NONE)
            {
                if(!sample_exists(c))
                {
                    c = sample_highest_valid_id();
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
                    c = vj_tag_highest_valid_id();
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
               int sample_offset = vj_tag_get_offset(args[0],args[1]);
               int sample_speed = 0;
               if( src == VJ_TAG_TYPE_NONE )
                   sample_speed = sample_get_speed(c);

                veejay_msg(VEEJAY_MSG_INFO, "Mixing with source (%s %d) at speed %d position %d", 
                    src==VJ_TAG_TYPE_NONE ? "sample" : "stream",c,sample_speed, sample_offset);
            }   
        }
    }
}

#define clamp_channel( a, b, c ) ( ( a < b ? c : (a >= c ? b : a )))

void vj_event_chain_entry_channel_dec(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[1];
	P_A(args,sizeof(args), NULL, 0,format,ap);

    if(SAMPLE_PLAYING(v))
    { 
        int entry = sample_get_selected_entry(v->uc->sample_id);
        int cha = sample_get_chain_channel(v->uc->sample_id,entry);
        int src = sample_get_chain_source(v->uc->sample_id,entry);
        int old = cha;
        if(src==VJ_TAG_TYPE_NONE)
        {   //decrease sample id
            cha = cha - args[0];
            if( sample_size() <= 0 )
            {
                veejay_msg(0, "No samples to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                sample_highest_valid_id() );

            if( !sample_exists( cha ) )
                cha = old;
        }
        else    
        {
            cha = cha - args[0];
            if( vj_tag_size() <= 0 )
            {
                veejay_msg(0, "No streams to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                vj_tag_highest_valid_id() );

            if( !vj_tag_exists( cha ))
                cha = old;
        }
        sample_set_chain_channel( v->uc->sample_id, entry, cha );
        veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d",entry,
                (src==VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),cha);
             
        if(v->no_bezerk) 
            veejay_set_frame(v , sample_get_resume(v->uc->sample_id));
    }
    if(STREAM_PLAYING(v))
    {
        int entry = vj_tag_get_selected_entry(v->uc->sample_id);
        int cha   = vj_tag_get_chain_channel(v->uc->sample_id,entry);
        int src   = vj_tag_get_chain_source(v->uc->sample_id,entry);
        int old = cha;  
        char description[100];

        if(src==VJ_TAG_TYPE_NONE)
        {   //decrease sample id
            cha = cha - args[0];
            if( sample_size() <= 0 )
            {
                veejay_msg(0, "No samples to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                sample_highest_valid_id() );
            if( !sample_exists(cha ) )
                cha = old;
        }
        else    
        {
            cha = cha - args[0];
            if( vj_tag_size() <= 0 )
            {
                veejay_msg(0, "No streams to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                vj_tag_highest_valid_id() );
            if(! vj_tag_exists( cha ))
                cha = old;
        }

        vj_tag_set_chain_channel( v->uc->sample_id, entry, cha );
        vj_tag_get_descriptive( cha, description);

        veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses Stream %d (%s)",entry,cha,description);
    }

}

void vj_event_chain_entry_channel_inc(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[1];
	P_A(args,sizeof(args), NULL,0,format,ap);

    if(SAMPLE_PLAYING(v))
    {
        int entry = sample_get_selected_entry(v->uc->sample_id);
        int cha = sample_get_chain_channel(v->uc->sample_id,entry);
        int src = sample_get_chain_source(v->uc->sample_id,entry);
        int old = cha;
        if(src==VJ_TAG_TYPE_NONE)
        {   //decrease sample id
            cha = cha + args[0];
            if( sample_size() <= 0 )
            {
                veejay_msg(0, "No samples to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                sample_highest_valid_id() );
            if( !sample_exists( cha ) )
                cha = old;
        }
        else    
        {
            cha = cha + args[0];
            if( vj_tag_size() <= 0 )
            {
                veejay_msg(0, "No streams to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                vj_tag_highest_valid_id() );
            if( !vj_tag_exists(cha) )
                cha = old;
        }
    
        sample_set_chain_channel( v->uc->sample_id, entry, cha );
        veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses %s %d",entry,
            (src==VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),cha);
//      if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);
        if(v->no_bezerk) veejay_set_frame(v,sample_get_resume(v->uc->sample_id));   
             
    }
    if(STREAM_PLAYING(v))
    {
        int entry = vj_tag_get_selected_entry(v->uc->sample_id);
        int cha   = vj_tag_get_chain_channel(v->uc->sample_id,entry);
        int src   = vj_tag_get_chain_source(v->uc->sample_id,entry);
        int old   = cha;
        char description[100];

        if(src==0)
        {   //decrease sample id
            cha = cha + args[0];
            if( sample_size() <= 0 )
            {
                veejay_msg(0, "No samples to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                sample_highest_valid_id() );
            if( !sample_exists( cha ) )
                cha = old;
        }
        else    
        {
            cha = cha + args[0];
            if( vj_tag_size() <= 0 )
            {
                veejay_msg(0, "No streams to mix with");
                return;
            }
            clamp_channel(
                cha,
                1,
                vj_tag_highest_valid_id() );
            if( !vj_tag_exists( cha ))
                cha = old;
        }

        vj_tag_set_chain_channel( v->uc->sample_id, entry, cha );
        vj_tag_get_descriptive( cha, description);
//      if(v->no_bezerk) veejay_set_sample(v, v->uc->sample_id);

        veejay_msg(VEEJAY_MSG_INFO, "Chain entry %d uses Stream %d (%s)",entry,
            vj_tag_get_chain_channel(v->uc->sample_id,entry),description);
    }
}

void vj_event_chain_entry_channel(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[3];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of bounds: %d", args[1]);
                return;
            }

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
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0]))
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

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
    P_A(args,sizeof(args), NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int source = args[2];
            int channel_id = args[3];
            int err = 1;
            if( source == VJ_TAG_TYPE_NONE && sample_exists(channel_id))
                err = 0;
            if( source != VJ_TAG_TYPE_NONE && vj_tag_exists(channel_id))
                err = 0;

    
            if( err == 0 &&
                sample_set_chain_source(args[0],args[1],source)!=-1 &&
                sample_set_chain_channel(args[0],args[1],channel_id) != -1)
            {
                veejay_msg(VEEJAY_MSG_INFO, "Selected input channel (%s %d) to mix in",
                    (source == VJ_TAG_TYPE_NONE ? "sample" : "stream") , channel_id);
                if( source != VJ_TAG_TYPE_NONE ) {
                    int slot = sample_has_cali_fx( args[0]);//@sample
                    if( slot >= 0 ) {
                        sample_cali_prepare( args[0],slot,channel_id);
                        veejay_msg(VEEJAY_MSG_DEBUG, "Using calibration data of stream %d",channel_id);
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
    if(STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0])) 
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
        
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int source = args[2];
            int channel_id = args[3];
            int err = 1;
            if( source == VJ_TAG_TYPE_NONE && sample_exists(channel_id))
                err = 0;
            if( source != VJ_TAG_TYPE_NONE && vj_tag_exists(channel_id))
                err = 0;

            //@ if there is CALI in FX chain,
            //@ call cali_prepare and pass channel id

                
            if( err == 0 &&
                vj_tag_set_chain_source(args[0],args[1],source)!=-1 &&
                vj_tag_set_chain_channel(args[0],args[1],channel_id) != -1)
            {
                veejay_msg(VEEJAY_MSG_INFO, "Selected input channel (%s %d) to mix in",
                    (source == VJ_TAG_TYPE_NONE ? "sample" : "stream") , channel_id);
                
                if( source != VJ_TAG_TYPE_NONE ) {
                    int slot = vj_tag_has_cali_fx( args[0]);
                        if( slot >= 0 ) {
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
	P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) 
    {
        int c = sample_get_selected_entry(v->uc->sample_id);
        int effect = sample_get_effect_any(v->uc->sample_id, c);
        int val = sample_get_effect_arg(v->uc->sample_id,c,args[0]);
        if ( vje_is_valid( effect  ) )
        {
            const char *effect_descr = vje_get_description(effect);
            const char *effect_param_descr = vje_get_param_description(effect,args[0]);
            int tval = val + args[1];
            if( tval > vje_get_param_max_limit( effect,args[0] ) )
                tval = vje_get_param_min_limit( effect,args[0]);
            else
                if( tval < vje_get_param_min_limit( effect,args[0] ) )
                    tval = vje_get_param_max_limit( effect,args[0] );
            if(sample_set_effect_arg( v->uc->sample_id, c,args[0],tval)!=-1 )
            {
                veejay_msg(VEEJAY_MSG_DEBUG,"Set \"%s\" parameter %d \"%s\" value %d",effect_descr, args[0], effect_param_descr, tval);
            }
        }
    }

    if(STREAM_PLAYING(v)) 
    {
        int c = vj_tag_get_selected_entry(v->uc->sample_id);
        int effect = vj_tag_get_effect_any(v->uc->sample_id, c);
        int val = vj_tag_get_effect_arg(v->uc->sample_id, c, args[0]);

        const char *effect_descr = vje_get_description(effect);
        const char *effect_param_descr = vje_get_param_description(effect,args[0]);
        int tval = val + args[1];

        if( tval > vje_get_param_max_limit( effect,args[0] ))
            tval = vje_get_param_min_limit( effect,args[0] );
        else
            if( tval < vje_get_param_min_limit( effect,args[0] ))
                tval = vje_get_param_max_limit( effect,args[0] );

        if(vj_tag_set_effect_arg(v->uc->sample_id, c, args[0], tval) )
        {
            veejay_msg(VEEJAY_MSG_DEBUG,"Set \"%s\" parameter %d \"%s\" value %d",effect_descr, args[0], effect_param_descr, tval );
        }
    }
}

void vj_event_chain_entry_set_arg_val(void *ptr, const char format[], va_list ap)
{
    int args[4];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    if(SAMPLE_PLAYING(v)) 
    {
        SAMPLE_DEFAULTS(args[0]);

        if(sample_exists(args[0]))
        {
            if(args[1] == -1) args[1] = sample_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int effect = sample_get_effect_any( args[0], args[1] );
            const char *effect_descr = vje_get_description(effect);
            const char *effect_param_descr = vje_get_param_description(effect,args[2]);
            if( vje_is_param_value_valid(effect,args[2],args[3]) )
            {
                if(sample_set_effect_arg( args[0], args[1], args[2], args[3])) {
                    veejay_msg(VEEJAY_MSG_INFO, "Set \"%s\" parameter %d \"%s\" to %d on Entry %d of Sample %d",
                               effect_descr, args[2], effect_param_descr, args[3],args[1],args[0]);
                }
            }
            else
            {
                veejay_msg(VEEJAY_MSG_ERROR, " \"%s\"  parameter %d \"%s\" with value %d invalid for Chain Entry %d of Sample %d",
                           effect_descr, args[2], effect_param_descr, args[3], args[1], args[0] );
            }
        } else { veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist", args[0]); }
    }
    if(STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);

        if(vj_tag_exists(args[0]))
        {
            if(args[1] == -1) args[1] = vj_tag_get_selected_entry(args[0]);
            if(v_chi(args[1]))
            {
                veejay_msg(VEEJAY_MSG_ERROR, "Chain index out of boundaries: %d", args[1]);
                return;
            }

            int effect = vj_tag_get_effect_any(args[0],args[1] );
            const char *effect_descr = vje_get_description(effect);
            const char *effect_param_descr = vje_get_param_description(effect,args[2]);
            if ( vje_is_param_value_valid( effect,args[2],args[3] ) )
            {
                if(vj_tag_set_effect_arg(args[0],args[1],args[2],args[3])) {
                    veejay_msg(VEEJAY_MSG_INFO,"Set \"%s\" parameter %d \"%s\" to %d on Entry %d of Stream %d",
                               effect_descr, args[2], effect_param_descr, args[3],args[2],args[1]);
                }
            }
            else {
                veejay_msg(VEEJAY_MSG_ERROR, "\"%s\" parameter %d \"%s\" with value %d for Chain Entry %d invalid for Stream %d",
                           effect_descr, args[2], effect_param_descr, args[3], args[1],args[0]);
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
    P_A(args,sizeof(args),NULL,0,format,ap);

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
	P_A(args,sizeof(args),NULL,0,format,ap);

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
	P_A(args,sizeof(args),NULL,0,format,ap);

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
    P_A(args,sizeof(args),NULL,0,format,ap);

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
            veejay_msg(VEEJAY_MSG_ERROR, "Invalid range given to crop %d - %d", args[0],args[1] );
        
    }
}

void vj_event_el_paste_at(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
	P_A(args,sizeof(args),NULL,0,format,ap);

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
            sample_set_startframe( v->uc->sample_id, args[0] );
            sample_set_endframe(   v->uc->sample_id, sample_video_length(v->uc->sample_id));
            constrain_sample( v, v->uc->sample_id );
        }

    }
}

void vj_event_el_save_editlist(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    char str[1024];
    int args[2];

    P_A(args,sizeof(args),str,sizeof(str), format,ap);
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
    char str[1024];
    P_A(NULL,0,str,sizeof(str),format,ap);

    if(SAMPLE_PLAYING(v))
    {
        if( !sample_usable_edl( v->uc->sample_id ))
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Cannot append video to a picture sample");
            return;
        }
    }

    if ( veejay_edit_addmovie(v,v->current_edit_list,str,start ))
        veejay_msg(VEEJAY_MSG_INFO, "Added video file %s to EditList",str); 
    else
        veejay_msg(VEEJAY_MSG_INFO, "Unable to add file %s to EditList",str); 
}

void vj_event_el_add_video_sample(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    char str[1024];
    int args[2];
    P_A(args,sizeof(args),str,sizeof(str),format,ap);

    int new_sample_id = args[0];
    if(new_sample_id == 0 )
    {
        veejay_msg(VEEJAY_MSG_INFO, "Create new sample from %s",
            str );
    }
    else
    {
        veejay_msg(VEEJAY_MSG_INFO, "Append %s to current sample",
            str );
    }
    new_sample_id = veejay_edit_addmovie_sample(v,str,new_sample_id );
    if(new_sample_id <= 0)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to open %s", str );
        new_sample_id = 0;
    }
}

void vj_event_tag_del(void *ptr, const char format[] , va_list ap ) 
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
	P_A(args,sizeof(args),NULL,0,format,ap);
    
    if(STREAM_PLAYING(v) && v->uc->sample_id == args[0])
    {
        veejay_msg(VEEJAY_MSG_INFO,"Cannot delete stream while playing");
    }
    else 
    {
        if(vj_tag_exists(args[0]))  
        {
            int i;
            for( i = 0; i < MAX_SEQUENCES ; i ++ ) {
                if( v->seq->samples[i].sample_id == args[0] && v->seq->samples[i].type != 0 ) {
                    v->seq->samples[i].sample_id = 0;
                    v->seq->samples[i].type = 0;
                }
            }

            if(vj_tag_del(args[0]))
            {
                veejay_msg(VEEJAY_MSG_INFO, "Deleted stream %d", args[0]);
                vj_tag_verify_delete( args[0], 1 );
            }
        }   
    }
}

void vj_event_tag_toggle(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[1];
	P_A(args,sizeof(args),NULL,0,format,ap);
    if(STREAM_PLAYING(v))
    {
        int active = vj_tag_get_active(v->uc->sample_id);
        vj_tag_set_active( v->uc->sample_id, !active);
        veejay_msg(VEEJAY_MSG_INFO, "Stream is %s", (vj_tag_get_active(v->uc->sample_id) ? "active" : "disabled"));
    }
}

void    vj_event_tag_new_generator( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    char str[255];
    int args[2];
    P_A(args,sizeof(args),str,sizeof(str),format,ap);

    int id = veejay_create_tag(v, VJ_TAG_TYPE_GENERATOR, str, v->nstreams,0,args[0]);

    if( id <= 0 ) {
        veejay_msg(0,"Error launching plugin '%s'", str );
    }
}

#ifdef USE_GDK_PIXBUF
void vj_event_tag_new_picture(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    char str[255];
    P_A(NULL,0,str,sizeof(str),format,ap);

    int id = veejay_create_tag(v, VJ_TAG_TYPE_PICTURE, str, v->nstreams,0,0);

    if(id <= 0 )
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new Picture stream");
}
#endif

#ifdef SUPPORT_READ_DV2
void    vj_event_tag_new_dv1394(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(args[0] == -1) args[0] = 63;
    veejay_msg(VEEJAY_MSG_DEBUG, "Try channel %d", args[0]);
    int id = veejay_create_tag(v, VJ_TAG_TYPE_DV1394, "/dev/dv1394", v->nstreams,0, args[0]);
    if( id <= 0)
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new DV1394 stream");
}
#endif

void    vj_event_v4l_blackframe( void *ptr, const char format[] , va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;

    int args[4];
    P_A(args,sizeof(args),NULL,0,format,ap);
    
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

void    vj_event_cali_write_file( void *ptr, const char format[], va_list ap)
{
    char str[1024];
    int args[2];
    veejay_t *v = (veejay_t*) ptr;

    P_A(args,sizeof(args),str,sizeof(str),format,ap);

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

void    vj_event_stream_new_clone( void *ptr, const char format[], va_list ap )
{
    int args[1];
	veejay_t *v = (veejay_t*) ptr;

    P_A(args,sizeof(args),NULL,0,format,ap);

    int id = veejay_create_tag( v, VJ_TAG_TYPE_CLONE, NULL, v->nstreams, args[0],args[0] );

    if( id <= 0 )
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create a clone of stream %d", args[0]);
    else
        veejay_msg(VEEJAY_MSG_ERROR, "Created a clone of stream %d", args[0]);
}

void    vj_event_stream_new_cali( void *ptr, const char format[], va_list ap)
{
    char str[1024];
    int args[2];
    veejay_t *v = (veejay_t*) ptr;

    P_A(args,sizeof(args),str,sizeof(str),format,ap);


    int id = veejay_create_tag(
            v, VJ_TAG_TYPE_CALI, 
            str, 
            v->nstreams, 
            0,0);
    if(id > 0 )
        v->nstreams++;

    if( id <= 0 )
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create load calibration file '%s'",str);
    else    
        veejay_msg(VEEJAY_MSG_INFO, "Loaded calibration file to Stream %d",id );


}

void vj_event_tag_new_avformat(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;

	char input[1024];
	P_A(NULL,0, input,sizeof(input),format,ap);

	int id = vj_tag_new(VJ_TAG_TYPE_AVFORMAT, input, v->nstreams,
                v->edit_list,
                v->pixel_format,
               	-1,
                -1,
                v->settings->composite );

    	if(id > 0 )
        	v->nstreams++;

   	if( id <= 0 )
        	veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new video stream ");
}

void vj_event_tag_new_v4l(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    char filename[255];
    P_A(args,sizeof(args),NULL,0,format,ap);

    snprintf(filename,sizeof(filename), "video%d", args[0]);

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

    if( id <= 0 )
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new Video4Linux stream ");
}

void vj_event_tag_new_net(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;

    char str[255];
    int args[2];

    P_A(args,sizeof(args), str, sizeof(str), format,ap);

    if( strncasecmp( str, "localhost",9 ) == 0 || strncasecmp( str, "127.0.0.1",9 ) == 0 )
    {
        if( args[0] == v->uc->port )
        {   
            veejay_msg(0, "Try another port number, I am listening on this one");
            return;
        }
    }
    
    int id = veejay_create_tag(v, VJ_TAG_TYPE_NET, str, v->nstreams, args[0],0);

    if(id <= 0)
        veejay_msg(VEEJAY_MSG_ERROR, "Failed to create unicast stream");
}

void vj_event_tag_new_mcast(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;

    char str[255];
    int args[3];

    P_A(args,sizeof(args), str,sizeof(str), format,ap);

    int id = veejay_create_tag(v, VJ_TAG_TYPE_MCAST, str, v->nstreams, args[0],0);

    if( id <= 0)
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new multicast stream");

}



void vj_event_tag_new_color(void *ptr, const char format[], va_list ap)
{
    veejay_t *v= (veejay_t*) ptr;
    int args[4];
    P_A(args,sizeof(args),NULL,0,format,ap);

    int i;
    for(i = 0 ; i < 3; i ++ )
        CLAMPVAL( args[i] );

    
    int id =  vj_tag_new( VJ_TAG_TYPE_COLOR, NULL, -1, v->edit_list,v->pixel_format, -1,0 , v->settings->composite);
    if(id > 0)
    {
        vj_tag_set_stream_color( id, args[0],args[1],args[2] );
    }   

    if( id <= 0 )
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new solid color stream");

}

void vj_event_tag_new_y4m(void *ptr, const char format[], va_list ap)
{
    veejay_t *v= (veejay_t*) ptr;
    char str[255];
    P_A(NULL,0, str,sizeof(str), format,ap);
    int id  = veejay_create_tag(v, VJ_TAG_TYPE_YUV4MPEG, str, v->nstreams,0,0);

    if( id <= 0 )
        veejay_msg(VEEJAY_MSG_INFO, "Unable to create new Yuv4mpeg stream");
}
void vj_event_v4l_set_brightness(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args), NULL,0,format,ap);
    
    STREAM_DEFAULTS(args[0]);
    
    if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
    {
        if(vj_tag_set_brightness(args[0],args[1]))
        {
            veejay_msg(VEEJAY_MSG_DEBUG,"Set brightness to %d",args[1]);
        }
    }
    
}

void    vj_event_vp_proj_toggle(void *ptr, const char format[],va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;

    if(!v->composite ) {
        veejay_msg(0, "No viewport active");
        return;
    }

    int mode = !composite_get_status(v->composite);
    composite_set_status( v->composite, mode );

    veejay_msg(VEEJAY_MSG_INFO, "Projection transform is now %s on startup",(mode==0? "inactive" : "active"));
}

void    vj_event_vp_stack( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(!v->composite ) {
        veejay_msg(0, "No viewport active");
        return;
    }

    if( args[0] == 1 )
    {
    }

    if ( args[1] == 1 ) {
        int mode = v->settings->composite;
        if( mode == 0 ) {
            v->settings->composite = 1;
        } else if ( mode == 1 ) {
            v->settings->composite = 0;
        } else if ( mode == 2 ) {
            v->settings->composite = 1;
        }
    } 
}

void    vj_event_vp_set_points( void *ptr, const char format[], va_list ap )
{
    int args[4];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(!v->composite ) {
        veejay_msg(0, "No viewport active");
        return;
    }

    if( args[0] <= 0 || args[0] > 4 ) {
        veejay_msg(0, "Invalid point number. Use 1 - 4");
        return;
    }
    if( args[1] < 0 ) {
        veejay_msg(0, "Scale must be a positive number");
        return;
    }
    float point_x =  ( (float) args[2] / (float) args[1] );
    float point_y =  ( (float) args[3] / (float) args[1] );

    v->settings->cx = point_x;
    v->settings->cy = point_y;
    v->settings->cn = args[0];
    v->settings->ca = 1;

}

void    vj_event_v4l_get_info(void *ptr, const char format[] , va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    STREAM_DEFAULTS(args[0]);

    char send_msg[128];
    char message[128];

    sprintf( send_msg, "000" );

    if(vj_tag_exists(args[0]))
    {
        int values[21];
        veejay_memset(values,0,sizeof(values));
        if(vj_tag_get_v4l_properties( args[0], values )) 
        {
            snprintf(message,sizeof(message), "%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d",
                values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8],values[9],
                values[10],values[11],values[12],values[13],values[14],values[15],values[16],values[17],values[18],values[19],values[20]    );
            FORMAT_MSG(send_msg, message);
        }
    }

    SEND_MSG( v,send_msg );
}

void vj_event_v4l_set_contrast(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args), NULL,0,format,ap);

    STREAM_DEFAULTS(args[0]);

    if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
    {
        if(vj_tag_set_contrast(args[0],args[1]))
        {
            veejay_msg(VEEJAY_MSG_DEBUG,"Set contrast to %d",args[1]);
        }
    }
}

void vj_event_v4l_set_white(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    STREAM_DEFAULTS(args[0]);

    if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
    {
        if(vj_tag_set_white(args[0],args[1]))
        {
            veejay_msg(VEEJAY_MSG_DEBUG,"Set whiteness to %d",args[1]);
        }
    }

}
void vj_event_v4l_set_saturation(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    STREAM_DEFAULTS(args[0]);

    if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
    {
#ifdef HAVE_V4L2 
        uint32_t v4l_ctrl_id = v4l2_get_property_id( "saturation" );
        if(v4l_ctrl_id == 0 ) {
            veejay_msg(0,"Invalid v4l2 property name 'saturation'" );
            return;
        }
        if(!vj_tag_v4l_set_control( args[0], v4l_ctrl_id, args[1] ) ) {
            veejay_msg(VEEJAY_MSG_DEBUG,"Not a valid video4linux device: %d", args[0] );
        }       
#endif
    }
}

void vj_event_v4l_set_color(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    STREAM_DEFAULTS(args[0]);

    if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
    {
        if(vj_tag_set_color(args[0],args[1]))
        {
            veejay_msg(VEEJAY_MSG_DEBUG,"Set color to %d",args[1]);
        }
    }

}

void vj_event_v4l_set_property( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    char str[255];
#ifdef HAVE_V4L2 
    P_A(args,sizeof(args),str,sizeof(str),format,ap);

    STREAM_DEFAULTS(args[0]);

    uint32_t v4l_ctrl_id = v4l2_get_property_id( str );
    if(v4l_ctrl_id == 0 ) {
        veejay_msg(VEEJAY_MSG_DEBUG,"Invalid v4l2 property name '%s'",str );
        return;
    }
    if(!vj_tag_v4l_set_control( args[0], v4l_ctrl_id, args[1] ) ) {
        veejay_msg(VEEJAY_MSG_DEBUG,"Not a valid video4linux device: %d", args[0] );
    }       
    else {
        veejay_msg(VEEJAY_MSG_DEBUG,"Set %s to %d", str, args[1] );
    }
#endif
}

void vj_event_v4l_set_hue(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    STREAM_DEFAULTS(args[0]);

    if(vj_tag_exists(args[0]) && STREAM_PLAYING(v))
    {
        if(vj_tag_set_hue(args[0],args[1]))
        {
            veejay_msg(VEEJAY_MSG_DEBUG,"Set hue to %d",args[1]);
        }
    }
}

void    vj_event_viewport_frontback(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    if(!v->composite) {
        veejay_msg(VEEJAY_MSG_ERROR, "No viewport active");
        return;
    }

    if( v->settings->composite == 2 && composite_get_ui( v->composite ) ) {
        if(v->use_osd==3) 
            v->use_osd = 0;
         composite_set_ui(v->composite, 0 );
         v->settings->composite = 0;
    }
    else {
        composite_set_ui( v->composite, 2 );
        v->settings->composite = 2;
        v->use_osd=3;
        veejay_msg(VEEJAY_MSG_INFO, "You can now calibrate your projection/camera, press CTRL-s again to save and exit");
    }
}

void    vj_event_toggle_transitions( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int i;
    if(v->settings->transition.global_state == 0) {

        int n = sample_highest();
        for( i = 1; i <= n; i ++ ) {
            if(!sample_exists(i))
                continue;
           // sample_set_transition_shape( i, -1 );
            sample_set_transition_active( i, 1 );
        }
        n = vj_tag_highest_valid_id();
        for( i = 1; i <= n; i ++ ) {
            if(!vj_tag_exists(i))
                continue;
           // vj_tag_set_transition_shape(i, -1);
            vj_tag_set_transition_active( i, 1 );
        }

        v->settings->transition.global_state = 1;
    }
    else {
        int n = sample_highest();
        for( i = 1; i <= n; i ++ ) {
            if(!sample_exists(i))
                continue;
            sample_set_transition_active( i, 0 );
        }
        n = vj_tag_highest_valid_id();
        for( i = 0; i <= n; i ++ ) {
            if(!vj_tag_exists(i))
                continue;
            vj_tag_set_transition_active(i, 0 );
        }
        
        v->settings->transition.global_state = 0;
    }

    veejay_msg(VEEJAY_MSG_INFO, "Transitions between samples %s",
            (v->settings->transition.global_state == 0 ? "disabled" : "enabled" ));
}

void    vj_event_toggle_osl( void *ptr, const char format[], va_list ap )
{
}

void    vj_event_toggle_osd( void *ptr, const char format[], va_list ap )
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
void    vj_event_toggle_copyright( void *ptr, const char format[], va_list ap )
{
    static int old_osd = -1;
    veejay_t *v = (veejay_t*) ptr;
    if( old_osd == -1 )
        old_osd = v->use_osd;
    if(v->use_osd == 0 || v->use_osd == 1)
        v->use_osd = 2;
    else
        v->use_osd = (old_osd==-1?0: old_osd);

    if(v->use_osd == 2 ) {
        if( veejay_log_to_ringbuffer()) 
            veejay_toggle_osl();
    } 

}
void    vj_event_toggle_osd_extra( void *ptr, const char format[], va_list ap )
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
    const char *name;
    int   id;
} recorder_formats[] = {
    { "mlzo", ENCODER_LZO },
    { "y4m422", ENCODER_YUV4MPEG },
    { "y4m420", ENCODER_YUV4MPEG420 },
    { "yv16", ENCODER_YUV422 },
    { "y422", ENCODER_YUV422 },
    { "i420", ENCODER_YUV420 },
    { "y420", ENCODER_YUV420 },
    { "huffyuv", ENCODER_HUFFYUV },
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
    P_A(args,sizeof(args),str,sizeof(str),format,ap);

    if(v->settings->tag_record || v->settings->offline_record || (v->seq->active && v->seq->rec_id) )
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

    int old_format = _recorder_format;

    veejay_msg(VEEJAY_MSG_DEBUG,"Current recording format is %s",
        vj_avcodec_get_encoder_name( old_format ));
        
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
        if(vj_el_is_dv(v->current_edit_list))
        {
            _recorder_format = ENCODER_DVVIDEO;
        }
        else
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Cannot set DVVideo format (invalid DV resolution)");
            _recorder_format = old_format;
        }

        veejay_msg(VEEJAY_MSG_INFO, "Selected recording format %s" , vj_avcodec_get_encoder_name(old_format));
        return;
    }

#ifdef HAVE_LIBQUICKTIME
    if(strncasecmp(str,"quicktime-dv", 12 ) == 0 )
    {
        if( vj_el_is_dv( v->current_edit_list ))
        {
            _recorder_format = ENCODER_QUICKTIME_DV;
        }
        else 
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Cannot set Quicktime-DV format (invalid DV resolution)");
            _recorder_format = old_format;
        }
    }
#endif
    veejay_msg(VEEJAY_MSG_INFO, "Selected recording format %s" , vj_avcodec_get_encoder_name(old_format));
}

static void _vj_event_tag_record( veejay_t *v , int *args )
{
    if(!STREAM_PLAYING(v))
    {
        p_invalid_mode();
        return;
    }

    char tmp[255];
    char prefix[1024];
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

    char sourcename[512];   
    vj_tag_get_description( v->uc->sample_id, sourcename );
    snprintf(prefix,sizeof(prefix),"%s-%02d-", sourcename, v->uc->sample_id);
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

    if( vj_tag_init_encoder( v->uc->sample_id, tmp, format, args[0]) <= 0 ) 
    {
        veejay_msg(VEEJAY_MSG_INFO, "Error trying to start recording from stream %d", v->uc->sample_id);
        if(!v->settings->offline_record || v->settings->offline_tag_id != v->uc->sample_id) {
            vj_tag_stop_encoder(v->uc->sample_id);
        }
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
    P_A(args,sizeof(args), NULL,0,format,ap);

    _vj_event_tag_record( v, args );
}

void vj_event_tag_rec_stop(void *ptr, const char format[], va_list ap) 
{
    char avi_file[1024];
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
        
        vj_tag_get_encoded_file(v->uc->sample_id, avi_file); 

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
            veejay_msg(VEEJAY_MSG_ERROR, "Cannot add videofile %s to EditList",avi_file);
        }

        veejay_msg(VEEJAY_MSG_ERROR, "Stopped recording from stream %d", v->uc->sample_id);
        vj_tag_reset_encoder( v->uc->sample_id);
        s->tag_record = 0;
        s->tag_record_switch = 0;

        if(play_now) 
        {
            int last_id = sample_highest_valid_id();
            veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d now", last_id );
            veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_SAMPLE, last_id );
        }
    }
}

void vj_event_tag_rec_offline_start(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[4];
	P_A(args,sizeof(args), NULL,0,format,ap);

    if( v->settings->offline_record )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Already recording from stream %d", v->settings->offline_tag_id);
        return;
    }
    
    if( v->settings->tag_record && STREAM_PLAYING(v) && v->uc->sample_id == v->settings->offline_tag_id)
    {
        veejay_msg(VEEJAY_MSG_ERROR ,"Please stop the stream recorder on stream %d first", v->uc->sample_id);
        return;
    }
    
    if( vj_tag_exists(args[0]))
    {
        vj_perform_start_offline_recorder(v,_recorder_format, args[0], args[1], args[2], args[3]);
    }
    else
    {
        veejay_msg(VEEJAY_MSG_ERROR, "(Offline) Unable to record from non-existing stream %d",args[0]);
    }
}

void vj_event_tag_rec_offline_stop(void *ptr, const char format[], va_list ap)
{
    char avi_file[1024];
    veejay_t *v = (veejay_t*)ptr;
    video_playback_setup *s = v->settings;
    if(s->offline_record) 
    {
        if( vj_tag_stop_encoder( s->offline_tag_id ) == 1 )
        {
            int id = 0;

            vj_tag_get_encoded_file(s->offline_tag_id, avi_file);

            if( v->settings->offline_linked_sample_id == -1 ) {
                id = 0;
            }
            else {
                id = v->settings->offline_linked_sample_id;
            }

            int new_id = vj_perform_commit_offline_recording(v, id, avi_file );

            vj_tag_reset_encoder(s->offline_tag_id);

            if(s->offline_created_sample && new_id > 0 )
            {
                veejay_msg(VEEJAY_MSG_INFO, "Playing sample %d now ",new_id );
                veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_SAMPLE,new_id );
            }

            veejay_msg(VEEJAY_MSG_INFO, "Stopped offline recorder");
        }
        s->offline_record = 0;
        s->offline_tag_id = 0;
        s->offline_created_sample = 0;
    }
    else {
        veejay_msg(0, "(Offline) recorder not active" );
    }
}


void vj_event_output_y4m_start(void *ptr, const char format[], va_list ap)
{
    veejay_msg(0, "Y4M out stream: obsolete - use recorder");
}

void vj_event_output_y4m_stop(void *ptr, const char format[], va_list ap)
{
    veejay_msg(0, "Y4M out stream: obsolete - use recorder");
}

void vj_event_enable_audio(void *ptr, const char format[], va_list ap)
{
#ifdef HAVE_JACK
    veejay_t *v = (veejay_t*)ptr;
    if (!v->audio_running )
    {
        veejay_msg(0,"Veejay was started without audio");
        return;
    }

    v->settings->audio_mute = 0;
#endif  
}

void vj_event_disable_audio(void *ptr, const char format[], va_list ap)
{
#ifdef HAVE_JACK
    veejay_t *v = (veejay_t *)ptr;
    if (!v->audio_running )
    {
        veejay_msg(0,"Veejay was started without audio");
        return;
    }
    v->settings->audio_mute = 1;
#endif
}

void vj_event_effect_inc(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
    P_A(args,sizeof(args),NULL,0,format,ap);  
    if(!SAMPLE_PLAYING(v) && !STREAM_PLAYING(v))
    {
        p_invalid_mode();
        return;
    }
    
    int max_fx_id = vje_get_last_id();

    v->uc->key_effect += args[0];
    while(!vje_is_valid( v->uc->key_effect ) ) {
        v->uc->key_effect += args[0];
        if( v->uc->key_effect > vje_max_space() )
            break;
    }
    
    if(v->uc->key_effect < VJ_IMAGE_EFFECT_MIN) 
        v->uc->key_effect = VJ_IMAGE_EFFECT_MIN;

    if(v->uc->key_effect > max_fx_id )
        v->uc->key_effect = VJ_IMAGE_EFFECT_MIN;
    if(v->uc->key_effect > VJ_VIDEO_EFFECT_MAX && v->uc->key_effect < VJ_PLUGIN)
        v->uc->key_effect = VJ_PLUGIN;

    veejay_msg(VEEJAY_MSG_INFO,"Selected FX [%d] [%s]",
            v->uc->key_effect, vje_get_description(v->uc->key_effect));

}

void vj_event_effect_dec(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1];
    P_A(args,sizeof(args),NULL,0,format,ap);
    if(!SAMPLE_PLAYING(v) && !STREAM_PLAYING(v))
    {
        p_invalid_mode();
        return;
    }

    int max_fx_id = vje_get_last_id();

    v->uc->key_effect -= args[0];
    while(!vje_is_valid( v->uc->key_effect ) ) {
        v->uc->key_effect -= args[0];
        if(v->uc->key_effect < VJ_IMAGE_EFFECT_MIN)
            break;
    }
    
    if(v->uc->key_effect < VJ_IMAGE_EFFECT_MIN) 
        v->uc->key_effect = max_fx_id;

    if(v->uc->key_effect > max_fx_id )
        v->uc->key_effect = max_fx_id;
    if(v->uc->key_effect > VJ_VIDEO_EFFECT_MAX && v->uc->key_effect < VJ_PLUGIN)
        v->uc->key_effect = VJ_PLUGIN;

    veejay_msg(VEEJAY_MSG_INFO,"Selected FX [%d] [%s]",
            v->uc->key_effect, vje_get_description(v->uc->key_effect));
}

void vj_event_effect_add(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    if(SAMPLE_PLAYING(v)) 
    {   
        int c = sample_get_selected_entry(v->uc->sample_id);

        if ( sample_chain_add( v->uc->sample_id, c, v->uc->key_effect ))
        {
            veejay_msg(VEEJAY_MSG_INFO,"Added Effect %s on chain entry %d",
                vje_get_description(v->uc->key_effect),
                c
            );
            if(v->no_bezerk && vje_get_extra_frame(v->uc->key_effect) ) 
            {
		        veejay_set_frame(v,sample_get_resume(v->uc->sample_id));
            }
            v->uc->chain_changed = 1;
        }
    }
    if(STREAM_PLAYING(v))
    {
        int c = vj_tag_get_selected_entry(v->uc->sample_id);
        if ( vj_tag_set_effect( v->uc->sample_id, c, v->uc->key_effect ))
        {
            veejay_msg(VEEJAY_MSG_INFO,"Added Effect %s on chain entry %d",
                vje_get_description(v->uc->key_effect),
                c
            );
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


void vj_event_resume_id(void *ptr, const char format[], va_list ap)
{
    veejay_t *v=  (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format, ap);

	if(STREAM_PLAYING(v)) {
		vj_event_select_id(ptr,format,ap);
	}
	else if(SAMPLE_PLAYING(v)) {
		int sample_id = (v->uc->sample_key*12)-12 + args[0];
		if(sample_exists(sample_id)) {
			if(sample_id != v->uc->sample_id) {
				long pos = sample_get_resume(sample_id);
				veejay_start_playing_sample(v, sample_id);
				veejay_set_frame(v,pos);
			}
		}
	}	

}


void vj_event_select_id(void *ptr, const char format[], va_list ap)
{
    veejay_t *v=  (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format, ap);
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

void	vj_event_macro_del(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
    int args[4];
    P_A( args,sizeof(args),NULL,0, format, ap );

	void *macro = NULL;
	if( SAMPLE_PLAYING(v) ) {
		macro = sample_get_macro( v->uc->sample_id );
	}
	if( STREAM_PLAYING(v)) {
		macro = vj_tag_get_macro( v->uc->sample_id );
	}

	if(macro) {
		vj_macro_del( macro, (long) args[0],args[1],args[2],args[3] );
	}
}

void	vj_event_macro_get_all(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
	void *macro = NULL;
	char *buf = NULL;
	if( SAMPLE_PLAYING(v) ) {
		macro = sample_get_macro( v->uc->sample_id );
	}
	if( STREAM_PLAYING(v)) {
		macro = vj_tag_get_macro( v->uc->sample_id );
	}

	if(macro) {
		buf = vj_macro_serialize(macro);
	}

	if( buf == NULL ) {
		SEND_MSG(v,"00000000");
	}
	else {
		SEND_MSG(v, buf);
	}	
}

void	vj_event_macro_put(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
    int args[3];
	char message[1024];
    P_A( args,sizeof(args),message,sizeof(message), format, ap );

	void *macro = NULL;
	if( SAMPLE_PLAYING(v) ) {
		macro = sample_get_macro( v->uc->sample_id );
	}
	if( STREAM_PLAYING(v)) {
		macro = vj_tag_get_macro( v->uc->sample_id );
	}

	if(vj_macro_put( macro, message, (long) args[0], args[1], args[2] )) {
		veejay_msg(VEEJAY_MSG_DEBUG, "Stored VIMS [%s] at frame position %d.%d, loop %d",
				message,args[0],args[1],args[2]);
	}else {
		veejay_msg(VEEJAY_MSG_ERROR, "Failed to store VIMS [%s] at frame position %d.%d, loop %d - Macro is full",
				message,args[0],args[1],args[2]);
	}
}

void	vj_event_macro_get(void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*) ptr;
    int args[3];
    P_A( args,sizeof(args),NULL,0, format, ap );

	void *macro = NULL;
	if( SAMPLE_PLAYING(v) ) {
		macro = sample_get_macro( v->uc->sample_id );
	}
	if( STREAM_PLAYING(v)) {
		macro = vj_tag_get_macro( v->uc->sample_id );
	}

	if(macro) {
		char *buf = vj_macro_serialize_macro(macro,args[0],args[1],args[2]);
		SEND_MSG(v, buf);
	}
	else {
		SEND_MSG(v,"00000000");
	}	
}

void    vj_event_select_macro( void *ptr, const char format[], va_list ap )
{
	veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A( args,sizeof(args),NULL,0, format, ap );

	void *macro = NULL;

	if(SAMPLE_PLAYING(v)) {
		macro = sample_get_macro(v->uc->sample_id);
	}
	if(STREAM_PLAYING(v)) {
		macro = vj_tag_get_macro(v->uc->sample_id);
	}

	if( macro ) {
		if(vj_macro_select( macro, args[0] )) {
    		veejay_msg(VEEJAY_MSG_INFO, "Changed VIMS macro bank to %d", args[0] );
		}
		else {
			veejay_msg(VEEJAY_MSG_ERROR, "Failed to change VIMS macro bank to %d", args[0]);
		}
	}
}

void vj_event_select_bank(void *ptr, const char format[], va_list ap) 
{
    veejay_t *v =(veejay_t*) ptr;
    int args[1];

	P_A(args,sizeof(args),NULL,0,format,ap);
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

    if(v->settings->tag_record) {
        veejay_msg(VEEJAY_MSG_INFO, "Stream '%s' [%d]/[%d] [%s] [transition%sactive (shape n°%02d>%d frames)] [recorded: %06ld frames]",
            title,id,vj_tag_size(),description,
            (vj_tag_get_transition_active(id) == 1 ? " " : " not " ),
            vj_tag_get_transition_shape(id),
            vj_tag_get_transition_length(id),
            vj_tag_get_encoded_frames(id));
    }
    else {
        veejay_msg(VEEJAY_MSG_INFO, "Stream '%s' [%d]/[%d] [%s] [transition%sactive (shape n°%02d>%d frames)]",
            title,id,vj_tag_size(),description,
            (vj_tag_get_transition_active(id) == 1 ? " " : " not " ),
            vj_tag_get_transition_shape(id),
            vj_tag_get_transition_length(id));
    }

    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
    {
        y = vj_tag_get_effect_any(id, i);
        if (y != -1) 
        {
            veejay_msg(VEEJAY_MSG_INFO, "%02d  [%d] [%s] %s (%s)",
                i,
                y,
                vj_tag_get_chain_status(id,i) ? "on" : "off", vje_get_description(y),
                (vje_get_subformat(y) == 1 ? "2x2" : "1x1")
            );


            char tmp[256] = {0};
            for (j = 0; j < vje_get_num_params(y); j++)
            {
                char small[32];
                value = vj_tag_get_effect_arg(id, i, j);
                snprintf( small,sizeof(small), "P%d = %d ",j,value );
                strcat( tmp, small );   
                }

            if (vje_get_extra_frame(y) == 1)
            {
                int source_type = vj_tag_get_chain_source(id, i);
                veejay_msg(VEEJAY_MSG_INFO, "Mixing with %s %d",(source_type == VJ_TAG_TYPE_NONE ? "Sample" : "Stream"),vj_tag_get_chain_channel(id,i));
                }
        }
    }
}

void vj_event_create_effect_bundle(veejay_t * v, char *buf, int key_id, int key_mod )
{
    char prefix[20];
    int i ,y,j;
    int num_cmd = 0;
    int id = v->uc->sample_id;
    int event_id = 0;
    int bunlen=0;

    if(!SAMPLE_PLAYING(v) && !STREAM_PLAYING(v)) 
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot take snapshot of Effect Chain");
        return;
    }

    char *blob = get_print_buf( 50 * SAMPLE_MAX_EFFECTS );
    if(!blob) {
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
        veejay_msg(VEEJAY_MSG_ERROR, "Effect Chain is empty" );    
        free(blob);     
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
                char bundle[512];
                int np = vje_get_num_params(y);
                sprintf(bundle, "%03d:0 %d %d 1", VIMS_CHAIN_ENTRY_SET_PRESET,i, effect_id );
                    for (j = 0; j < np; j++)
                {
                    char svalue[32];
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
                int blen = strlen(bundle);
                veejay_strncpy( blob+bunlen, bundle,blen);
                bunlen += blen;
            }
        }
    }
    sprintf(prefix, "BUN:%03d{", num_cmd);
    sprintf(buf, "%s%s}",prefix,blob);
    event_id = vj_event_suggest_bundle_id();

    if(event_id <= 0 )  
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Cannot add more bundles");
    }
    else {
        vj_msg_bundle *m = vj_event_bundle_new( buf, event_id);
        if(!m)
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Unable to create new Bundle");
        }
        else {
            if(!vj_event_bundle_store(m))
                veejay_msg(VEEJAY_MSG_ERROR, "Error storing Bundle %d", event_id);
        }
    }

    free(blob);
}


void vj_event_print_sample_info(veejay_t *v, int id) 
{
    video_playback_setup *s = v->settings;
    int y, i, j;
    long value;
    char timecode[48];
    char curtime[48];
    char sampletitle[200];
    MPEG_timecode_t tc;
    y4m_ratio_t ratio = mpeg_conform_framerate( (double)v->current_edit_list->video_fps );
    long start = sample_get_startFrame( id );
    long end = sample_get_endFrame( id );
    long speed = sample_get_speed(id);
    long len = end - start;

    veejay_memset( &tc,0,sizeof(MPEG_timecode_t));
    mpeg_timecode(&tc, len, mpeg_framerate_code( ratio ),v->current_edit_list->video_fps);
    snprintf(timecode,sizeof(timecode), "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);

    mpeg_timecode(&tc,  s->current_frame_num, mpeg_framerate_code(ratio),v->current_edit_list->video_fps);
    snprintf(curtime, sizeof(curtime), "%2d:%2.2d:%2.2d:%2.2d", tc.h, tc.m, tc.s, tc.f);
    sample_get_description( id, sampletitle );

    veejay_msg(VEEJAY_MSG_INFO, 
        "Sample %s [%4d]/[%4d]\t[duration: %s | %8ld] @%8ld %s",
        sampletitle,id,sample_size(),timecode,len, (long)v->settings->current_frame_num,
        curtime);
    
    if(sample_encoder_active(v->uc->sample_id))
    {
        veejay_msg(VEEJAY_MSG_INFO, "REC %09d\t[timecode: %s | %8ld ]",
            sample_get_frames_left(v->uc->sample_id),
            curtime,(long)v->settings->current_frame_num);

    }
    
    veejay_msg(VEEJAY_MSG_INFO, 
        "[%09ld]>[%09ld]@%4.2f [speed %d] [%s-loop] [transition%sactive (shape n°%02d>%d frames)]",
        start,end, (float)speed * v->current_edit_list->video_fps,speed,
        (sample_get_looptype(id) == 2 ? "pingpong" : (sample_get_looptype(id)==1 ? "normal" : (sample_get_looptype(id)==3 ? "random" : "no"))),
        (sample_get_transition_active(id) == 1 ? " " : " not " ), sample_get_transition_shape(id), sample_get_transition_length(id));


    for (i = 0; i < SAMPLE_MAX_EFFECTS; i++)
    {
        y = sample_get_effect_any(id, i);
        if (y != -1)
        {
        
            char tmp[256] = { 0 };
            for (j = 0; j < vje_get_num_params(y); j++)
            {
                char small[32];
                value = sample_get_effect_arg(id, i, j);
                
                snprintf(small, sizeof(small), "P%d = %ld ",j, value );
                strcat( tmp, small );
            }

            veejay_msg(VEEJAY_MSG_INFO, "%02d | %03d | %s |%s %s {%s}",
                i,
                y,
                sample_get_chain_status(id,i) ? "on " : "off", vje_get_description(y),
                (vje_get_subformat(y) == 1 ? "2x2" : "1x1"),
                tmp
            );

                if (vje_get_extra_frame(y) == 1)
            {
                int source = sample_get_chain_source(id, i);
                int sample_offset = sample_get_offset(id,i);
                int c = sample_get_chain_channel(id,i);
                int sample_speed = 0;
                if( source == VJ_TAG_TYPE_NONE )
                   sample_speed = sample_get_speed(c);
     
                veejay_msg(VEEJAY_MSG_INFO, "Mixing with %s %d at speed %d, position %d",(source == VJ_TAG_TYPE_NONE ? "sample" : "stream"),
                        c,
                        sample_speed,
                        sample_offset );
                }
        }
        }

    if(  sample_get_editlist(id) == v->current_edit_list ) {
        veejay_msg(VEEJAY_MSG_DEBUG, "Sample is using EDL from plain video");
    } else {
        veejay_msg(VEEJAY_MSG_DEBUG, "Sample is using its own EDL");
    }

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
	P_A(args,sizeof(args),NULL,0,format,ap);
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

void    vj_event_send_track_list        (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    char *s_print_buf = get_print_buf(0);
    sprintf(s_print_buf, "%05d",0);
    int n = vj_tag_highest();
    if (n >= 1 )
    {
        char *print_buf = get_print_buf(SEND_BUF);
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
                    snprintf(space,sizeof(space)-1,"%s %d", tag->descr, tag->id );
                    snprintf(cmd,sizeof(cmd)-1,"%03zu%s",strlen(space),space);
                    APPEND_MSG(print_buf,cmd); 
                }
            }
        }
        sprintf(s_print_buf, "%05zu%s",strlen(print_buf),print_buf);
        free(print_buf);
    }

    SEND_MSG(v,s_print_buf);
    free(s_print_buf);
}

void    vj_event_send_tag_list          (   void *ptr,  const char format[],    va_list ap  )
{
    int args[1];
    
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args), NULL,0,format,ap);
    int i,n;
    char *s_print_buf = get_print_buf(0);
    sprintf(s_print_buf, "%05d",0);
    int start_from_tag = 1;
    if(args[0]>0) 
        start_from_tag = args[0];

    n = vj_tag_highest();
    if (n >= 1 )
    {
        char line[300];
        char *print_buf = get_print_buf(SEND_BUF);

        for(i=start_from_tag; i <= n; i++)
        {
            if(vj_tag_exists(i) &&!vj_tag_is_deleted(i))
            {   
                vj_tag *tag = vj_tag_get(i);
                char source_name[255];
                char cmd[300];
                vj_tag_get_source_name( i, source_name );
                snprintf(line,sizeof(line),"%05d%02d%03d%03d%03d%03d%03zu%s",
                    i,
                    vj_tag_get_type(i),
                    tag->color_r,
                    tag->color_g,
                    tag->color_b,
                    tag->opacity, 
                    strlen(source_name),
                    source_name
                );
                snprintf(cmd,sizeof(cmd), "%03zu%s",strlen(line),line);
                APPEND_MSG(print_buf,cmd); 
            }
        }
        sprintf(s_print_buf, "%05zu%s",strlen(print_buf),print_buf);
        free(print_buf);
    }

    SEND_MSG(v,s_print_buf);
    free(s_print_buf);
}

static  char *_vj_event_gatter_sample_info( veejay_t *v, int id )
{
    char description[SAMPLE_MAX_DESCR_LEN];
    int end_frame   = sample_get_endFrame( id );
    int start_frame = sample_get_startFrame( id );
    char timecode[20];
    MPEG_timecode_t tc;
    y4m_ratio_t ratio = mpeg_conform_framerate( (double) v->current_edit_list->video_fps );
    mpeg_timecode( &tc, (end_frame - start_frame + 1),mpeg_framerate_code(ratio),v->current_edit_list->video_fps );

    sprintf( timecode, "%2d:%2.2d:%2.2d:%2.2d", tc.h,tc.m,tc.s,tc.f );
    sample_get_description( id, description );

    int dlen = strlen(description);
    int tlen = strlen(timecode);    
    char *s_print_buf = get_print_buf(512);
    snprintf( s_print_buf, 512,
        "%08d%03d%s%03d%s%02d%04d", 
        ( 3 + dlen + 3+ tlen + 2 + 4),
        dlen,
        description,
        tlen,
        timecode,
        0,
        id
    );  
    return s_print_buf;
}
static  char *  _vj_event_gatter_stream_info( veejay_t *v, int id )
{
    char description[SAMPLE_MAX_DESCR_LEN];
    char source[255];
    int  stream_type = vj_tag_get_type( id );
    vj_tag_get_source_name( id, source );
    vj_tag_get_description( id, description );
    
    int dlen = strlen( description );
    int tlen = strlen( source );
    char *s_print_buf = get_print_buf( 512 );
    snprintf( s_print_buf,512,
        "%08d%03d%s%03d%s%02d%02d",
        (  3 + dlen + 3 + tlen + 2 + 2),
        dlen,
        description,
        tlen,
        source,
        stream_type,
        id 
    );
    return s_print_buf;
}

void    vj_event_send_sample_info       (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    int failed = 1;
    P_A(args,sizeof(args),NULL,0,format,ap);

    char *s_print_buf = NULL;

    switch( args[1] )
    {
        case 0:
            SAMPLE_DEFAULTS(args[0]);

            if(sample_exists(args[0]))
            {
                s_print_buf = _vj_event_gatter_sample_info(v,args[0]);
                failed = 0;
            }
            break;
        case  1:
            STREAM_DEFAULTS(args[0]);

            if(vj_tag_exists(args[0]))
            {
                s_print_buf = _vj_event_gatter_stream_info(v,args[0]);  
                failed = 0;
            }
            break;
        default:
            break;
    }
    
    if(failed) {
        s_print_buf = get_print_buf( 9 );
        snprintf( s_print_buf,9, "%08d", 0 );
    }
    SEND_MSG(v , s_print_buf );
    free(s_print_buf);
}

void    vj_event_get_image_part         (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[5];
    P_A(args,sizeof(args),NULL,0,format,ap);

    int w=0,h=0,x=0,y=0;
    int y_only = 0;
    x = args[0]; 
    y = args[1];
    w = args[2];
    h = args[3];
    y_only = args[4];

    if( y_only < 0 || y_only > 1 ) {
        veejay_msg(0,"Please specify 0 for full chroma, 1 for luminance only (greyscale)");
        SEND_MSG(v, "00000000" );
        return;
    }

    if( x < 0 || x > v->video_output_width || y < 0 || y > v->video_output_height ||
            w < 0 || w > (v->video_output_width - x) ||
            h < 0 || h > (v->video_output_height -y) )
    {
        veejay_msg(0, "Invalid image region, use [start x, start y, box width, box height]");
        SEND_MSG(v, "00000000" );
        return;
    }

    VJFrame frame;
    veejay_memcpy(&frame, v->effect_frame1, sizeof(VJFrame));
    vj_perform_get_primary_frame( v, frame.data );

    int ux = x;
    int uy = y;

    int uw = w >> v->effect_frame1->shift_h;
    int uh = h >> v->effect_frame1->shift_v;

    ux = ux >> v->effect_frame1->shift_h; 
    uy = uy >> v->effect_frame1->shift_v;

    int result = composite_get_original_frame( v->composite, frame.data,
                          frame.data,
                          v->settings->composite,
                              y,
                          h );

    if( result == -1 ) {
        composite_get_top( v->composite,frame.data, frame.data, v->settings->composite );
    }

    int len = (w * h);
    if( y_only == 0 ) {
        len += (uw * uh);
        len += (uw * uh);
    }

    uint8_t *tmp = (uint8_t*) vj_malloc (sizeof(uint8_t) * len);
    
    if(!tmp) {
        veejay_msg(0, "Memory allocation error");
        SEND_MSG(v, "00000000" );
        return;
    }

    uint8_t *start_addr = tmp;
    unsigned int i,j;

    int nobackplane = 0;
    if( result == -1 )
        nobackplane = 1;

    int bh = ( h - y ); //@ composite copies from row start to row end in new buffer
    if( nobackplane )
        bh = h + y; //@ but if composite has no mirror plane, fetch the pixels directly from final frame

    int bw = w + x; //@ width is unchanged (composite copies image rows)

    for( i = (nobackplane ? y : 0); i < bh; i ++ ) {
        for( j = x; j < bw; j ++ ) {
            *(tmp++) = frame.data[0][i * frame.width + j];
        }
    }

    if( y_only == 0 ) {
        int ubh = (uh - uy);
        if( nobackplane )
            ubh = uh + uy;

        int ubw = uw + ux;

        for( i = (nobackplane ? uy : 0); i < ubh; i ++ ) {
            for( j = ux; j < ubw; j ++ ) {
                *(tmp++) = frame.data[1][i * frame.uv_width + j];
            }
        }

        for( i = (nobackplane ? uy : 0); i < ubh; i ++ ) {
            for( j = ux; j < ubw; j ++ ) {
                *(tmp++) = frame.data[2][i * frame.uv_height + j];
            }
        }
    }

    char header[9];
    snprintf( header, sizeof(header), "%08d",len );
    SEND_DATA(v, header, 8 );
    SEND_DATA(v, start_addr, len );
    free(start_addr);
}

void    vj_event_get_scaled_image       (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    int w=0,h=0;
    int max_w = vj_perform_preview_max_width(v);
    int max_h = vj_perform_preview_max_height(v);
        
    w = args[0]; 
    h = args[1];

    if( w <= 0 || h <= 0 || w >= max_w || h >= max_h )
    {
        veejay_msg(0, "Invalid image dimension %dx%d requested (max is %dx%d)",w,h,max_w,max_h );
        SEND_MSG(v, "00000000" );
        return;
    }

    int dstlen = 0;
    VJFrame frame;
    veejay_memcpy(&frame, v->effect_frame1, sizeof(VJFrame));
    vj_perform_get_primary_frame( v, frame.data );
    if( use_bw_preview_ ) {
        vj_fastbw_picture_save_to_mem(
                &frame,
                w,
                h,
                vj_perform_get_preview_buffer(v));
        dstlen = w * h;
    }
    else {
        vj_fast_picture_save_to_mem(
                &frame,
                w,
                h,
                vj_perform_get_preview_buffer(v));
        dstlen = (w * h) + ((w*h)/4) + ((w*h)/4);
    }

    char header[9];
    snprintf( header,sizeof(header), "%06d%1d%1d", dstlen, use_bw_preview_, yuv_get_pixel_range() );
    SEND_DATA(v, header, 8 );
    SEND_DATA(v, vj_perform_get_preview_buffer(v), dstlen );
}

void    vj_event_get_cali_image     (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    int id   = args[0];
    int type = args[1];
    
    if( !vj_tag_exists(id) )
    {
        SEND_MSG(v, "000000000" );
        return;
    }

    int total_len = 0;
    int uv_len    = 0;
    int len       = 0;

    uint8_t *buf = vj_tag_get_cali_buffer( id , type,  &total_len, &len, &uv_len );

    if( buf == NULL ) {
        SEND_MSG(v, "00000000"  );
    }
    else {
        char header[64];
        snprintf( header,sizeof(header), "%03d%08d%06d%06d%06d%06d",8+6+6+6+6,len, len, 0, v->video_output_width, v->video_output_height );
        SEND_MSG( v, header );
        int res = vj_server_send(v->vjs[VEEJAY_PORT_CMD], v->uc->current_link, buf,len);
        if(!res) {
            veejay_msg(0,"Failed to send calibration image. Header: [%s]",header);
        }
    }
}

void    vj_event_toggle_bw( void *ptr, const char format[], va_list ap )
{
    if( use_bw_preview_ )
        use_bw_preview_ = 0;
    else
        use_bw_preview_ = 1;
    veejay_msg(VEEJAY_MSG_INFO, "Live image viewer is %s", (use_bw_preview_ ? "in color" : "in gray" ));
}

void    vj_event_send_working_dir(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    char str[2048];
    P_A(args,sizeof(args),str,sizeof(str), format,ap);

    
    filelist_t *list = (filelist_t*)find_media_files(v);
    char *s_print_buf = NULL;
    
    if(!list) {
        
        s_print_buf = get_print_buf( 9 );
        sprintf(s_print_buf,"%08d",0);

    }else {

        int len = 0;
        int i;
        //@ length of file names
        for( i = 0; i < list->num_files; i ++ ) {
            len += ( list->files[i] == NULL ? 0 : strlen( list->files[i] ) + 4 );
        }

        s_print_buf = get_print_buf( len + 9 );
        sprintf( s_print_buf, "%08d", len );

        for( i = 0; i <list->num_files; i ++ ) {
            char tmp[PATH_MAX];

            if(list->files[i]==NULL)
                continue;

            snprintf(tmp,sizeof(tmp), "%04zu%s",strlen( list->files[i] ), list->files[i] );

            strcat( s_print_buf,tmp);
        }

        free_media_files(v,list);
    }

    SEND_MSG(v, s_print_buf );
    free( s_print_buf );
}

void	vj_event_get_feedback		(   void *ptr, const char format[], va_list ap)
{
	veejay_t *v = (veejay_t*)ptr;
	char message[16];
	char *s_print_buf = get_print_buf(0);

	sprintf(message, "%d", v->settings->feedback);
	FORMAT_MSG(s_print_buf, message);
	SEND_MSG(v, s_print_buf);
}

void    vj_event_send_sample_list       (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    int start_from_sample = 1;
    char cmd[512];
    char line[512];
    int i,n;
    P_A(args,sizeof(args),NULL,0,format,ap);
    if(args[0] > 0 )
        start_from_sample = args[0];
    char *s_print_buf = get_print_buf(0);

    sprintf(s_print_buf, "00000000");

    n = sample_highest();
    if( n > 1 )
    {
        char *print_buf = get_print_buf(SEND_BUF);
        for(i=start_from_sample; i <= n; i++)
        {
            if(sample_exists(i))
            {   
                char description[SAMPLE_MAX_DESCR_LEN];
                int end_frame = sample_get_endFrame(i);
                int start_frame = sample_get_startFrame(i);
                /* format of sample:
                    00000 : id
                    000000000 : start    
                                    000000000 : end
                                    xxx: str  : description
                */
                sample_get_description( i, description );
                
                snprintf(cmd,sizeof(cmd),"%05d%09d%09d%03zu%s",
                    i,
                    start_frame,    
                    end_frame,
                    strlen(description),
                    description
                );
                FORMAT_MSG(line,cmd);
                APPEND_MSG(print_buf,line); 
            }

        }
        sprintf(s_print_buf, "%08zu%s", strlen(print_buf),print_buf);
        free(print_buf);
    }
    SEND_MSG(v, s_print_buf);
    free(s_print_buf);
}

void    vj_event_send_sample_stack      (   void *ptr,  const char format[],    va_list ap )
{
    char line[32];
    int args[4];
    char    buffer[1024];
    char    message[1024];  
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

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
                sample_len = vj_tag_get_n_frames( channel );
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
            offset  = vj_tag_get_offset( args[0], i );
            if( source == 0 )
                sample_len= sample_video_length( channel );
            else 
                sample_len = vj_tag_get_n_frames( channel );

            snprintf( line, sizeof(line), "%02d%04d%02d%08d%08d",i,channel,source, offset, sample_len );
            strncat( buffer, line, strlen(line));
        }
    }   

    FORMAT_MSG( message, buffer );
    SEND_MSG(   v, message );

}

void    vj_event_send_stream_args       (   void *ptr, const char format[],     va_list ap )
{

    char fline[1024];
    char line[8192];
    int args[4];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    const char *dummy = "000";

    if(STREAM_PLAYING(v)) 
    {
        STREAM_DEFAULTS(args[0]);

        if( vj_tag_get_type(args[0]) == VJ_TAG_TYPE_GENERATOR ) {
            int tagargs[MAX_ARGUMENTS];
            int n_args = 0;
            int id = 0;
            veejay_memset( tagargs, 0, sizeof(tagargs));
            vj_tag_generator_get_args( args[0], tagargs, &n_args, &id );

            char *line_ptr = &line[0];

            line_ptr = vj_sprintf( line_ptr, id ); *line_ptr ++ = ' ';

            int n = n_args;
            int i;
            for( i = 0; i < n; i ++ ) {
                line_ptr = vj_sprintf( line_ptr, tagargs[i] ); *line_ptr ++ = ' ';
            }

            line_ptr = vj_sprintf( line_ptr, tagargs[n] );

            FORMAT_MSG(fline,line);
            SEND_MSG(v, fline);
            return;
        }
    }

    SEND_MSG(v, dummy);
}

void    vj_event_send_generator_list( void *ptr, const char format[], va_list ap )
{
    int total = 0;
    int *generators = plug_find_all_generator_plugins( &total );
    int i;
    char *s_print_buf = get_print_buf(6 + (total * 128));
    veejay_t *v = (veejay_t*) ptr;
    if( s_print_buf == NULL ) {
        SEND_MSG(v, "00000" );  
    }
    else {
    
        char *print_buf = get_print_buf( total * 128 );
        char  line[128];

        for( i = 0; i < total; i ++ ) {
            char *name = plug_get_so_name_by_idx( generators[i] );
            if(name == NULL)
                continue;
    
            int   name_len = strlen(name);

            snprintf( line, sizeof(line), "%03d%.124s", name_len, name );
            APPEND_MSG( print_buf, line );
        
            free(name);
        }

        sprintf( s_print_buf, "%05zu%s", strlen( print_buf ), print_buf );
        free(print_buf);
        free(generators);

        SEND_MSG(v, s_print_buf);
        free(s_print_buf);
    }
}

void    vj_event_send_chain_entry       (   void *ptr,  const char format[],    va_list ap  )
{
    char fline[1024];
    char line[1024];
    int args[4];
    int error = 1;
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);
    sprintf(line, "%03d", 0);

    char param[1024];

    if( SAMPLE_PLAYING(v)  )
    {
        SAMPLE_DEFAULTS(args[0]);

        if(args[1]==-1)
            args[1] = sample_get_selected_entry(args[0]);

        int effect_id = sample_get_effect_any(args[0], args[1]);
        
        if(effect_id > 0)
        {
            int is_video = vje_get_extra_frame(effect_id);
            int params[SAMPLE_MAX_PARAMETERS];
            int p;
            int video_on = sample_get_chain_status(args[0],args[1]);
            int num_params = vje_get_num_params(effect_id);
            int kf_type = 0;
            int kf_status = sample_get_kf_status( args[0],args[1],&kf_type );
            int transition_enabled = 0;
            int transition_loop = 0;
            int subrender_entry = sample_entry_is_rendering(args[0],args[1]);

            for(p = 0 ; p < num_params; p++)
                params[p] = sample_get_effect_arg(args[0],args[1],p);
            for(p = num_params; p < SAMPLE_MAX_PARAMETERS; p++)
                params[p] = 0;

            snprintf( param, sizeof(param), "%d %d %d %d %d %d %d %d %d %d %d ", 
                   effect_id,
                   is_video,
                   num_params,
                   kf_status,
                   kf_type,
                   transition_enabled, transition_loop, 
                   sample_get_chain_source(args[0],args[1]),
                   sample_get_chain_channel(args[0],args[1]),
                   video_on,
                   subrender_entry);

            strncat( line, param, strlen(param));
            for(p = 0; p < num_params - 1; p ++ ) {
               /* int kfe_start = 0;
                int kfe_end = 0;
                int kfe_type = 0;
                int kfe_status = 0;
                sample_get_kf_tokens( args[0],args[1], p, &kfe_start, &kfe_end, &kfe_type, &kfe_status );
                snprintf(param,sizeof(param), "%d %d %d %d %d ", params[p], kfe_start, kfe_end, kfe_type, kfe_status ); */
                snprintf(param,sizeof(param), "%d ",params[p]);
                strncat( line, param,strlen(param));
            }
            snprintf(param, sizeof(param),"%d",params[p]);
            strncat( line,param,strlen(param));

            error = 0;
        }
    }
    
    if(STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);
        if(args[1] == -1)
            args[1] = vj_tag_get_selected_entry(args[0]);

        int effect_id = vj_tag_get_effect_any(args[0], args[1]);

        if(effect_id > 0)
        {
            int is_video = vje_get_extra_frame(effect_id);
            int params[SAMPLE_MAX_PARAMETERS];
            int p;
            int num_params = vje_get_num_params(effect_id);
            int video_on = vj_tag_get_chain_status(args[0], args[1]);
            int kf_type = 0;
            int kf_status = vj_tag_get_kf_status( args[0],args[1], &kf_type );
            int transition_enabled = 0;
            int transition_loop = 0;
            int subrender_entry = vj_tag_entry_is_rendering(args[0],args[1]);

            for(p = 0 ; p < num_params; p++)
                params[p] = vj_tag_get_effect_arg(args[0],args[1],p);
            for(p = num_params; p < SAMPLE_MAX_PARAMETERS;p++)
                params[p] = 0;

            snprintf( param, sizeof(param), "%d %d %d %d %d %d %d %d %d %d %d ", 
                    effect_id, 
                    is_video, 
                    num_params,  
                   kf_status,
                   kf_type,
                   transition_enabled,
                   transition_loop,
                   vj_tag_get_chain_source(args[0],args[1]),
                   vj_tag_get_chain_channel(args[0],args[1]),
                   video_on,
                   subrender_entry);

            strncat( line, param, strlen(param));
            for(p = 0; p < num_params - 1; p ++ ) {
               /* int kfe_start = 0;
                int kfe_end = 0;
                int kfe_type = 0;
                int kfe_status = 0;
                vj_tag_get_kf_tokens( args[0],args[1], p, &kfe_start, &kfe_end, &kfe_type, &kfe_status );
                snprintf(param,sizeof(param), "%d %d %d %d %d ", params[p], kfe_start, kfe_end, kfe_type, kfe_status );*/
                snprintf(param,sizeof(param), "%d ",params[p]);
                strncat( line, param,strlen(param));
            }
            snprintf(param, sizeof(param),"%d",params[p]);
            strncat( line,param,strlen(param));

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


void    vj_event_send_chain_entry_parameters    (   void *ptr,  const char format[],    va_list ap  )
{
    char fline[1024];
    char line[1024];
    int args[4];
    int error = 1;
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args),NULL,0,format,ap);
    snprintf(line,sizeof(line), "%03d", 0);

    char param[1024];

    if( SAMPLE_PLAYING(v)  )
    {
        SAMPLE_DEFAULTS(args[0]);

        if(args[1]==-1)
            args[1] = sample_get_selected_entry(args[0]);

        int effect_id = sample_get_effect_any(args[0], args[1]);
        
        if(effect_id > 0)
        {
            int is_video = vje_get_extra_frame(effect_id);
            int params[SAMPLE_MAX_PARAMETERS];
            int p;
            int video_on = sample_get_chain_status(args[0],args[1]);
            int num_params = vje_get_num_params(effect_id);
            int kf_type = 0;
            int kf_status = sample_get_kf_status( args[0],args[1],&kf_type );

            for(p = 0 ; p < num_params; p++)
                params[p] = sample_get_effect_arg(args[0],args[1],p);
            for(p = num_params; p < SAMPLE_MAX_PARAMETERS; p++)
                params[p] = 0;

            snprintf( param, sizeof(param), "%d %d %d %d 0 0 %d %d %d %d 0 ", effect_id, is_video, num_params,
                   kf_type,kf_status,
                   sample_get_chain_source(args[0],args[1]),
                   sample_get_chain_channel(args[0],args[1]),
                   video_on);

            strncat( line, param, strlen(param));
            for(p = 0; p < num_params - 1; p ++ ) {
                snprintf(param,sizeof(param), "%d %d %d %d ", params[p],
                    vje_get_param_min_limit( effect_id, p ),
                    vje_get_param_max_limit( effect_id, p ),
                    vje_get_param_default( effect_id,p )    
                    );
                strncat( line, param,strlen(param));
            }
            snprintf(param, sizeof(param),"%d %d %d %d",params[p],
                    vje_get_param_min_limit( effect_id, p ),
                    vje_get_param_max_limit( effect_id, p ),
                    vje_get_param_default( effect_id,p )    
                    );

            strncat( line,param,strlen(param));

            error = 0;
        }
    }
    
    if(STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);

        if(args[1] == -1)
            args[1] = vj_tag_get_selected_entry(args[0]);

        int effect_id = vj_tag_get_effect_any(args[0], args[1]);

        if(effect_id > 0)
        {
            int is_video = vje_get_extra_frame(effect_id);
            int params[SAMPLE_MAX_PARAMETERS];
            int p;
            int num_params = vje_get_num_params(effect_id);

            int video_on = vj_tag_get_chain_status(args[0], args[1]);
            int kf_type = 0;
            int kf_status = vj_tag_get_kf_status( args[0],args[1], &kf_type );

            for(p = 0 ; p < num_params; p++)
                params[p] = vj_tag_get_effect_arg(args[0],args[1],p);
            for(p = num_params; p < SAMPLE_MAX_PARAMETERS;p++)
                params[p] = 0;

            snprintf( param, sizeof(param), "%d %d %d %d 0 0 %d %d %d %d 0 ", effect_id, is_video, num_params,  
                   kf_type,
                       kf_status,          
                   vj_tag_get_chain_source(args[0],args[1]),
                   vj_tag_get_chain_channel(args[0],args[1]),
                   video_on);

            strncat( line, param, strlen(param));
            for(p = 0; p < num_params - 1; p ++ ) {
                snprintf(param,sizeof(param), "%d %d %d %d ", params[p],
                    vje_get_param_min_limit( effect_id, p ),
                    vje_get_param_max_limit( effect_id, p ),
                    vje_get_param_default( effect_id,p )    
                    );

                strncat( line, param,strlen(param));
            }
            snprintf(param, sizeof(param),"%d %d %d %d",params[p],
                    vje_get_param_min_limit( effect_id, p ),
                    vje_get_param_max_limit( effect_id, p ),
                    vje_get_param_default( effect_id,p )    
                    );

            strncat( line,param,strlen(param));

            error = 0;
        }
    }

    if(!error)
    {
        snprintf(fline,sizeof(fline),"%04zu%s",strlen(line),line);
        SEND_MSG(v, fline);
    }
    else {
        SEND_MSG(v,line);
    }
}

void    vj_event_send_chain_list        (   void *ptr,  const char format[],    va_list ap  )
{
    int i;
    char line[VIMS_CHAIN_LIST_ENTRY_LENGTH+1]; // null terminated buffer
    int args[1];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    char *s_print_buf = get_print_buf(0);
    sprintf( s_print_buf, "%03d",0 );

    if(SAMPLE_PLAYING(v))
    {
        SAMPLE_DEFAULTS(args[0]);
        char *print_buf = get_print_buf( 1 + (VIMS_CHAIN_LIST_ENTRY_LENGTH * SAMPLE_MAX_EFFECTS ));
        for(i=0; i < SAMPLE_MAX_EFFECTS; i++)
        {
            int effect_id = sample_get_effect_any(args[0], i);
            if(effect_id > 0)
            {
                int is_video = vje_get_extra_frame(effect_id);
                int using_effect = sample_get_chain_status(args[0], i);
                int using_audio = 0;
                int chain_source = sample_get_chain_source(args[0], i);
                int chain_channel = sample_get_chain_channel(args[0], i);
                int kf_type = 0;
                int kf_status = sample_get_kf_status( args[0], i, &kf_type );
                int subrender_entry = 0;
                sample_get_subrender(args[0], i, &subrender_entry);

                sprintf(line, VIMS_CHAIN_LIST_ENTRY_FORMAT,
                    i,
                    effect_id,
                    is_video,
                    (using_effect <= 0  ? 0 : 1 ),
                    (using_audio  <= 0  ? 0 : 1 ),
                    chain_source,
                    chain_channel,
                    kf_status,
                    subrender_entry
                );
                        
                APPEND_MSG(print_buf,line);
            }
        }
        sprintf(s_print_buf, "%03zu%s",strlen(print_buf), print_buf);
        free(print_buf);

    } 
    else if(STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);
        char *print_buf = get_print_buf(1 + (VIMS_CHAIN_LIST_ENTRY_LENGTH * SAMPLE_MAX_EFFECTS));

        for(i=0; i < SAMPLE_MAX_EFFECTS; i++) 
        {
            int effect_id = vj_tag_get_effect_any(args[0], i);
            if(effect_id > 0)
            {
                int is_video = vje_get_extra_frame(effect_id);
                int using_effect = vj_tag_get_chain_status(args[0],i);
                int chain_source = vj_tag_get_chain_source(args[0], i);
                int chain_channel = vj_tag_get_chain_channel(args[0], i);
                int kf_type = 0;
                int kf_status = vj_tag_get_kf_status( args[0], i, &kf_type ); // exist for streaù ? or 0 ?
                int subrender_entry = 0;
                vj_tag_get_subrender(args[0],i,&subrender_entry);
                sprintf(line, VIMS_CHAIN_LIST_ENTRY_FORMAT,
                    i,
                    effect_id,
                    is_video,
                    (using_effect <= 0  ? 0 : 1 ),
                    0,
                    chain_source,
                    chain_channel,
                    kf_status,
                    subrender_entry
                );
                APPEND_MSG(print_buf, line);
            }
        }
        sprintf(s_print_buf, "%03zu%s",strlen( print_buf ), print_buf);
        free(print_buf);
    } else {
        sprintf(s_print_buf, "000");
    }
    SEND_MSG(v, s_print_buf);
    free(s_print_buf);
}
void    vj_event_send_shm_info( void *ptr, const char format[], va_list ap)
{
    int args[1] = { -1 };
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    net_set_screen_id( args[0] );

    char *msg = get_print_buf(128);
    snprintf( msg, 128, 
           "%d %d %d %d",
            v->video_output_width,
            v->video_output_height,
            v->pixel_format,
            vj_shm_get_my_id( v->shm ) );
    
    int  msg_len = strlen(msg);
    char *tmp = get_print_buf(1 + msg_len + 3);

    sprintf( tmp, "%03d%s",msg_len, msg );
                    
    SEND_MSG(v,tmp);
    free(msg);
    free(tmp);
    
    if( args[0] >= 0 ) {
        veejay_msg(VEEJAY_MSG_INFO, "Binding this instance to screen %d of remote",
                args[0]);
    }
}


void    vj_event_send_video_information     (   void *ptr,  const char format[],    va_list ap  )
{
    /* send video properties */
    char info_msg[150];
    veejay_t *v = (veejay_t*)ptr;

    editlist *el = v->current_edit_list;
/*
    editlist *el = ( SAMPLE_PLAYING(v) ? sample_get_editlist( v->uc->sample_id ) : 
                v->current_edit_list );
*/
    long n_frames = el->total_frames;
    if( SAMPLE_PLAYING(v))
        n_frames = sample_max_video_length( v->uc->sample_id );
    char *s_print_buf = get_print_buf(200);
    snprintf(info_msg,sizeof(info_msg)-1, "%04d %04d %01d %c %02.3f %1d %04d %06ld %02d %03ld %08ld %1d %d",
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
        v->audio,
        v->settings->use_vims_mcast
        );  
    sprintf(s_print_buf, "%03zu%s",strlen(info_msg), info_msg);
    SEND_MSG(v,s_print_buf);
    free(s_print_buf);
}

void    vj_event_send_editlist          (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*) ptr;
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
    

    char *s_print_buf = get_print_buf( b + 8 );
    snprintf( s_print_buf, (b+8),"%06d%s", b, msg );
    if(msg)free(msg);
    SEND_MSG( v, s_print_buf );
    free(s_print_buf);
}

void    vj_event_send_frame             (   void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[1] = { -1 };

    if( v->splitter ) {
        P_A(args,sizeof(args),NULL,0,format,ap);
    }

    int i = 0;
    
    for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ ) {
        if( v->rlinks[i] == -1 ) {
            v->rlinks[i] = v->uc->current_link;
            if( v->splitter ) {
                v->splitted_screens[ i ] = args[0];
            }
            break;
        }
    }

    if (!v->settings->is_dat )
    {
        veejay_msg(0, "Wrong control port for retrieving frames");
        SEND_MSG(v, "00000000000000000000"); //@ send empty header only (20 bytes)
        return;
    }

    v->settings->unicast_frame_sender = 1;
}


void    vj_event_mcast_start                (   void *ptr,  const char format[],    va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    char s[255];    
    P_A( args,sizeof(args), s ,sizeof(s), format, ap);

    if(!v->settings->use_vims_mcast) {
        veejay_msg(VEEJAY_MSG_ERROR, "start veejay in multicast mode (see -V commandline option)");
    }
    else
    {
        v->settings->mcast_frame_sender = 1;
        v->settings->mcast_mode = args[0];
        vj_server_set_mcast_mode( v->vjs[2],args[0] );
        veejay_msg(VEEJAY_MSG_INFO, "Veejay started multicast frame sender");
    }
}


void    vj_event_mcast_stop             (   void *ptr,  const char format[],    va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    if(!v->settings->use_vims_mcast)
        veejay_msg(VEEJAY_MSG_ERROR, "start veejay in multicast mode (see -V commandline option)");
    else
    {
        v->settings->mcast_frame_sender = 0;
        veejay_msg(VEEJAY_MSG_INFO, "Veejay stopped multicast frame sender");
    }
}

void    vj_event_send_effect_list       (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int i;
    char *priv_msg = NULL;
    int len = 0;
    int n_fx = vje_max_space();

    for( i = 0; i < n_fx; i ++ ) {
        len += vje_get_summarylen( i );
    }

    priv_msg = (char*) vj_malloc(sizeof(char) * (len+6+1) );
    sprintf(priv_msg, "%06d", len );
    
    for(i=0; i < n_fx; i++)
    {
        char line[4096];
        if(vje_get_summary(i,line))
        {
            char fline[5000];
            int line_len = strlen(line);
            snprintf(fline,sizeof(fline), "%04d%s", line_len, line );
            veejay_strncat( priv_msg, fline, line_len + 4 );
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
        char *ptr;
        buf[strlen(buf)-1] = 0;
        event_name = strtok_r(buf, "|",&ptr);
        event_msg = strtok_r(NULL, "|",&ptr);
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
    P_A( args,sizeof(args), s ,sizeof(s), format, ap);
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
    int args[4];
    char value[100];
    int mode = 0;
    

    P_A( args, sizeof(args), value,sizeof(value), format ,ap );

    if( args[1] <= 0 || args[1] >= SDL_NUM_SCANCODES)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Invalid key identifier %d (range is 1 - %d)", args[1], SDL_NUM_SCANCODES);
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
    P_A(args,sizeof(args),NULL,0,format,ap);
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
    
    int args[2];
    char s[1024];
    P_A(args,sizeof(args),s,sizeof(s),format,ap);

    if(args[0] == 0)
    {
        args[0] = vj_event_suggest_bundle_id();
    }

    if(args[0] < VIMS_BUNDLE_START|| args[0] > VIMS_BUNDLE_END )
    {
        // invalid bundle
        veejay_msg(VEEJAY_MSG_ERROR, "VIMS Bundle identifiers range from %d-%d", VIMS_BUNDLE_START, VIMS_BUNDLE_END);
        return;
    }
    // allocate new
    veejay_strrep( s, '_', ' ');
    vj_msg_bundle *m = vj_event_bundle_new(s, args[0]);
    if(!m)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to add new VIMS Bundle");
        return;
    }

    // bye existing bundle
    if( vj_event_bundle_exists(args[0]))
    {
        veejay_msg(VEEJAY_MSG_DEBUG,"(VIMS) Bundle exists - replacing contents");
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

void    vj_event_set_stream_arg( void *ptr, const char format[], va_list ap)
{
    long int tmp = 0;
    int base = 10;
    int index = 1; 
    int args[MAX_ARGUMENTS];
    char str[1024]; 
    char *end = str;
    veejay_t *v = (veejay_t*)ptr;
    veejay_memset(args,0,sizeof(args));
   
    P_A(args,sizeof(args),str,sizeof(str), format,ap);

    while( (tmp = strtol( end, &end, base ))) {
        args[index] = (int) tmp;
        index ++;
    }

    int *n_args = &args[1];

    STREAM_DEFAULTS(args[0]);

    if(STREAM_PLAYING(v)) 
    {
        if( vj_tag_get_type(args[0]) == VJ_TAG_TYPE_GENERATOR ) {
            vj_tag_generator_set_arg( args[0], n_args );
        }
    }

}

void    vj_event_set_stream_color(void *ptr, const char format[], va_list ap)
{
    int args[4];
    P_A(args,sizeof(args),NULL,0,format,ap);
    veejay_t *v = (veejay_t*) ptr;
    
    STREAM_DEFAULTS(args[0]);
    // allow changing of color while playing plain/sample
    if(vj_tag_exists(args[0]) && vj_tag_get_type(args[0]) == VJ_TAG_TYPE_COLOR )
    {
        CLAMPVAL( args[1] );
        CLAMPVAL( args[2] );
        CLAMPVAL( args[3] );    
        vj_tag_set_stream_color(args[0],args[1],args[2],args[3]);
    }
    else
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Solid stream %d does not exist",
            args[0]);
    }
}

#ifdef USE_GDK_PIXBUF
void vj_event_screenshot(void *ptr, const char format[], va_list ap)
{
    int args[4];
    char filename[1024];
    P_A(args,sizeof(args), filename,sizeof(filename), format, ap );
    veejay_t *v = (veejay_t*) ptr;

    char type[5] = { 0 };

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
    P_A(args,sizeof(args), filename,sizeof(filename), format, ap );
    veejay_t *v = (veejay_t*) ptr;

    v->uc->hackme = 1;
    v->uc->filename = vj_strdup( filename );
}
#endif
#endif

void        vj_event_quick_bundle( void *ptr, const char format[], va_list ap)
{
    vj_event_commit_bundle( (veejay_t*) ptr,0,0);
}

void    vj_event_vloopback_start(void *ptr, const char format[], va_list ap)
{
    int args[5];
    char device_name[100];

    memset( &args, -1, sizeof(args));

    P_A(args,sizeof(args),NULL,0,format,ap);
    
    veejay_t *v = (veejay_t*)ptr;

    sprintf(device_name, "/dev/video%d", args[0] );

    veejay_msg(VEEJAY_MSG_INFO, "Open vloopback %s", device_name );

    v->vloopback = vj_vloopback_open( device_name, v->effect_frame1, args[1], args[2],vj_vloopback_get_pixfmt( args[3] ) ); 
    if(v->vloopback == NULL)
    {
        veejay_msg(VEEJAY_MSG_ERROR,
            "Cannot open vloopback %s", device_name );
        return;
    }
}

void    vj_event_vloopback_stop( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    vj_vloopback_close( v->vloopback );
    v->vloopback = NULL;
}
/* 
 * Function that returns the options for a special sample (markers, looptype, speed ...) or
 * for a special stream ... 
 *
 * Needs two Parameters, first on: -1 last created sample, 0 == current playing sample, >=1 id of sample
 * second parameter is the playmode of this sample to decide if its a video sample or any kind of stream
 * (for this see comment on void vj_event_send_sample_info(..) 
 */ 
void vj_event_send_sample_options   (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    int id=0;
    int failed = 1; 

    P_A(args,sizeof(args),NULL,0,format,ap);
    
    char options[256];
    char prefix[4];

    char *s_print_buf = get_print_buf(128);
    int values[21];

    switch(args[1])
    {
        case VJ_PLAYBACK_MODE_SAMPLE: 

        SAMPLE_DEFAULTS(args[0]);
        
        id = args[0];

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
    
                    snprintf( options,sizeof(options),"%06d%06d%03d%02d%06d%06d%01d",start,end,speed,loop,marker_start,marker_end,effects_on);
                    failed = 0;
                    snprintf(prefix,sizeof(prefix), "%02d", 0 );
                }   
            }
            break;
        case VJ_PLAYBACK_MODE_TAG:  

        STREAM_DEFAULTS(args[0]);
    
        id = args[0];
    
        if(vj_tag_exists(id)) 
            {
            /* For gathering further informations of the stream first decide which type of stream it is 
               the types are definded in libstream/vj-tag.h and uses then the structure that is definded in 
               libstream/vj-tag.h as well as some functions that are defined there */
                vj_tag *si = vj_tag_get(id);
                int stream_type = si->source_type;
                snprintf(prefix,sizeof(prefix), "%02d", stream_type );
                if (stream_type == VJ_TAG_TYPE_COLOR)
                {
                    int col[3] = {0,0,0};
                    col[0] = si->color_r;
                    col[1] = si->color_g;
                    col[2] = si->color_b;
                
                    snprintf( options,sizeof(options),"%03d%03d%03d",col[0],col[1],col[2]);
                    failed = 0;
                }
            /* this part of returning v4l-properties is here implemented again ('cause there is
             * actually a VIMS-command to get these values) to get all necessary stream-infos at 
             * once so only ONE VIMS-command is needed */
                else if (stream_type == VJ_TAG_TYPE_V4L)
                {
                    vj_tag_get_v4l_properties(id,values);
            
                    snprintf( options,sizeof(options),
                        "%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d",
                        values[0],
                        values[1],
                        values[2],
                        values[3],
                        values[4],
                        values[5],
                        values[6],
                        values[7],
                        values[8],
                        values[9],
                        values[10],
                        values[11],
                        values[12],
                        values[13],
                        values[14],
                        values[15],
                        values[16],
                        values[17],
                        values[18],
                        values[19],
                        values[20]
                        );
                    failed = 0;
                }
                else    
                {
                    int effects_on = si->effect_toggle;
                    snprintf( options,sizeof(options), "%01d",effects_on);
                    failed = 0;
                }
            }
            break;
        default:
            break;      
        }   

    if(failed)
        sprintf( s_print_buf, "%05d", 0 );
    else
        sprintf( s_print_buf, "%05zu%s%s",strlen(prefix) + strlen(options), prefix,options );

    SEND_MSG(v , s_print_buf );
    free(s_print_buf);
}


void    vj_event_set_shm_status( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    if(!v->shm) {
        //@ try it anyway
        v->shm = vj_shm_new_master( v->homedir, v->effect_frame1 );
        if(!v->shm) {
            return;
        }
    }

    if( args[0] == 0 ) {
        vj_shm_set_status( v->shm, 0 );
    } else {
        vj_shm_set_status( v->shm, 1 );
    }

}

void    vj_event_get_shm( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*)ptr;
    char tmp[32];
    if(!v->shm) {
        snprintf(tmp,sizeof(tmp)-1,"%016d",0);
        SEND_MSG(v, tmp );
        return;
    }

    snprintf(tmp, sizeof(tmp)-1, "%016d", vj_shm_get_my_id( v->shm ) );

    SEND_MSG(v, tmp );
}
void    vj_event_offline_samples(void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*)ptr;
    int i;
    for( i = 0; i < VJ_MAX_CONNECTIONS;  i ++ ) {
        if( v->rmodes[i] == -1000 )
            continue;
        v->rmodes[i] = VJ_PLAYBACK_MODE_SAMPLE;
    }
    veejay_msg(VEEJAY_MSG_INFO, "Okay, I will force play-mode depending VIMS events to sample mode for link %d", v->uc->current_link);
}
void    vj_event_offline_tags( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t *)ptr;
    int i;
    for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ ) {
        if( v->rmodes[i] == -1000 )
            continue;
        v->rmodes[ i ] = VJ_PLAYBACK_MODE_TAG;
    }
    veejay_msg(VEEJAY_MSG_INFO, "Okay, I will force play-mode depending VIMS events to stream mode for link %d", v->uc->current_link);
}

void    vj_event_playmode_rule( void *ptr, const char format[],  va_list ap )
{
    veejay_t *v = (veejay_t *)ptr;
    int i;
    for( i = 0; i < VJ_MAX_CONNECTIONS; i ++ ) {
        if( v->rmodes[i] == -1000 ) {
            continue;
        }
        v->rmodes[ i ] = -1;
    }
    veejay_msg(VEEJAY_MSG_INFO, "Okay, play-mode depending VIMS for link %d no longer enforced", v->uc->current_link);
}

void    vj_event_connect_shm( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    if( args[0] == v->uc->port ) {
        veejay_msg(0, "Cannot pull info from myself inside VIMS event");
        return;
    }

    int32_t key = vj_share_pull_master( v->shm,"127.0.0.1", args[0] );
    int id = veejay_create_tag( v, VJ_TAG_TYPE_GENERATOR, "lvd_shmin.so", v->nstreams, 0, key);
    
    if( id <= 0 ) {
        veejay_msg(0, "Unable to connect to shared resource id %d", key );
    }
}

void    vj_event_connect_split_shm( void *ptr, const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*) ptr;
    int args[2];
    P_A(args,sizeof(args), NULL,0,format,ap);
    
    int32_t key = args[0];
    int safe_key = vj_shm_get_id();

    vj_shm_set_id( key );

    veejay_msg(VEEJAY_MSG_INFO,"Connect to shared memory resource %x (%d)", key,key);

    int id = veejay_create_tag( v, VJ_TAG_TYPE_GENERATOR, "lvd_shmin.so", v->nstreams, 0, key);
    
    vj_shm_set_id( safe_key );
    
    if( id <= 0 ) {
        veejay_msg(0, "Unable to connect to shared resource id %d", key );
    }

    veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG ,id);
}

#ifdef HAVE_FREETYPE
void    vj_event_get_srt_list(  void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    char *str = NULL;
    int len = 0;

    if(!v->font)
    {
        SEND_MSG(v, "000000" );
        return;
    }

    void *font = vj_font_get_dict( v->font );
    if(!font) {
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

    for ( i = 0; list[i] != NULL; i ++ ) { }

    str = vj_calloc( len + (i*2) + 6 );
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

void    vj_event_get_font_list( void *ptr,  const char format[],    va_list ap  )
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
void    vj_event_get_srt_info(  void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args), NULL,0,format,ap);

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
    char *str = vj_calloc( len+20 );
    sprintf(str,"%06d%s",len,sequence);
    free(sequence); 
    
    SEND_MSG(v , str );
}

void    vj_event_save_srt(  void *ptr,  const char format[],    va_list ap  )
{
    char file_name[512];
    int args[1];
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args),file_name,sizeof(file_name),format,ap);

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
void    vj_event_load_srt(  void *ptr,  const char format[],    va_list ap  )
{
    char file_name[512];
    int args[1];
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args),file_name,sizeof(file_name), format,ap);

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

void    vj_event_select_subtitle(   void *ptr,  const char format[],    va_list ap  )
{
    int args[6];
    veejay_t *v = (veejay_t*)ptr;

    if(!v->font)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "No font renderer active");
        return;
    }
    
    P_A(args,sizeof(args), NULL,0,format,ap);

    vj_font_set_current( v->font, args[0] );
}


void    vj_event_get_keyframes( void *ptr,  const char format[],    va_list ap  )
{
    int args[3];
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args), NULL,0,format,ap);

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
    }
    else if (STREAM_PLAYING(v))
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

void    vj_event_set_kf_status_param( void *ptr, const char format[], va_list ap)
{
    int args[4];
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v))
    {
        SAMPLE_DEFAULTS(args[0]);

        keyframe_set_param_status( args[0], args[1], args[2], args[3], 1 );
    }
    else if (STREAM_PLAYING(v))
    {
        STREAM_DEFAULTS(args[0]);
        keyframe_set_param_status( args[0], args[1], args[2], args[3], 0 );
    }
}

void    vj_event_set_kf_status( void *ptr,  const char format[],    va_list ap  )
{
    int args[3];
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args), NULL,0,format,ap);

    if(SAMPLE_PLAYING(v))
    {
        sample_chain_set_kf_status( v->uc->sample_id, args[0],args[1] );
        sample_set_kf_type( v->uc->sample_id,args[0],args[2]);
        veejay_msg(VEEJAY_MSG_INFO, "Sample %d is using animated parameter values", v->uc->sample_id);
    } else if (STREAM_PLAYING(v))
    {
        vj_tag_chain_set_kf_status(v->uc->sample_id,args[0],args[1] );
        vj_tag_set_kf_type(v->uc->sample_id,args[0],args[2]);
        veejay_msg(VEEJAY_MSG_INFO, "Stream %d is using animated parameter values", v->uc->sample_id);

    }
}
void    vj_event_reset_kf( void *ptr,   const char format[],    va_list ap  )
{
    int args[3];
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args), NULL,0,format,ap);

    if(SAMPLE_PLAYING(v))
    {
        sample_chain_reset_kf( v->uc->sample_id, args[0] );
    } else if (STREAM_PLAYING(v))
    {
        vj_tag_chain_reset_kf( v->uc->sample_id, args[0] );
    }
}

void    vj_event_del_keyframes( void *ptr, const char format[], va_list ap )
{
    int args[3];
    veejay_t *v = (veejay_t*)ptr;

    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v))
    {
        keyframe_clear_entry( v->uc->sample_id, args[0], args[1], 1 );
    }
    else if (STREAM_PLAYING(v)) {
        keyframe_clear_entry( v->uc->sample_id, args[0], args[1], 0 );
    }
}

static  void    *select_dict( veejay_t *v , int n )
{
    if( SAMPLE_PLAYING(v) )
        return sample_get_dict( n );
    else if(STREAM_PLAYING(v))
        return vj_tag_get_dict( n );
    return NULL;
}

void    vj_event_add_subtitle(  void *ptr,  const char format[],    va_list ap  )
{
    char text[2048];
    int args[6];
    int k;
    veejay_t *v = (veejay_t*)ptr;

    if(!v->font)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "No font renderer active");
        return;
    }

    P_A(args,sizeof(args),text,sizeof(text), format,ap);

    void *dict = select_dict( v, v->uc->sample_id );
    if(!dict)
    {
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
            text[k] = 0x20;
    }
        
    if( args[3] < 0 || args[4] < 0 ||
            args[3] >= v->video_output_width ||
            args[4] >= v->video_output_height )
    {
        veejay_msg(VEEJAY_MSG_ERROR,
                "Invalid XY position");
        return;
    }

    vj_font_set_dict( v->font, dict );

    int id = vj_font_new_text( v->font, (unsigned char*) text, (long) args[1], (long)args[2], args[0] );
    
    vj_font_set_position( v->font, args[3] ,args[4] );

    char newslot[16];
    sprintf(newslot, "%05d%05d",5, id );
    SEND_MSG(v,newslot);    
}
void    vj_event_upd_subtitle(  void *ptr,  const char format[],    va_list ap  )
{
    int args[5]; 
    char text[2048];

    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),text,sizeof(text), format,ap);

    if(!v->font )
    {
        veejay_msg(0, "No font renderer active");
        return;
    }

    void *dict = select_dict( v, v->uc->sample_id );
    if(dict)
    {
        vj_font_set_dict( v->font, dict );
        vj_font_update_text( v->font, (long) args[1], (long) args[2], args[0], text );
    }
}

void    vj_event_del_subtitle(  void *ptr,  const char format[],    va_list ap  )
{
    int args[5];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);
    
    if(!v->font)
    {
        veejay_msg(0, "No font renderer active");
        return;
    }


    void *dict = select_dict( v, v->uc->sample_id );
    if(dict)
    {
        vj_font_set_dict( v->font, dict );
        vj_font_del_text( v->font, args[0] );
    }
}

void    vj_event_font_set_position( void *ptr,  const char format[],    va_list ap  )
{
    int args[5];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(!v->font)
    {
        veejay_msg(0, "No font renderer active");
        return;
    }

    void *dict = select_dict( v, v->uc->sample_id );
    if(dict)
    {
        vj_font_set_dict( v->font, dict );
        vj_font_set_position( v->font, args[0] ,args[1] );
    }
}

void    vj_event_font_set_color(    void *ptr,  const char format[],    va_list ap  )
{
    int args[6];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(!v->font)
    {
        veejay_msg(0, "No font renderer active");
        return;
    }

    void *dict = select_dict( v, v->uc->sample_id );
    if(dict)
    {
        vj_font_set_dict( v->font, dict );

        switch( args[4] )
        {
            case 0:
                vj_font_set_outline_and_border(v->font, args[0],args[1]  );
                break;
            case 1:
                vj_font_set_fgcolor( v->font,args[0],args[1],args[2],args[3] );
                break;
            case 2:
                vj_font_set_bgcolor( v->font,args[0],args[1],args[2],args[3] );
                break;
            case 3:
                vj_font_set_lncolor( v->font,args[0],args[1],args[2],args[3] );
            break;
            default:
                veejay_msg(0, "Invalid mode. Use 0=outline/border 1=FG,2=BG,3=LN" );
                break;
        }
    }
}
void    vj_event_font_set_size_and_font(    void *ptr,  const char format[],    va_list ap  )
{
    int args[5];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);
 
    if(!v->font)
    {
        veejay_msg(0, "No font renderer active");
        return;
    }
 
    void *dict = select_dict( v, v->uc->sample_id );
    if(dict)
    {
        vj_font_set_dict( v->font, dict );
        vj_font_set_size_and_font(v->font, args[0],args[1]);
    }
}
#endif

static void vj_event_sample_next1( veejay_t *v )
{
    if( v->seq->active ){
        int s = (v->settings->current_playback_speed < 0 ? -1 : 1 );
        int p = v->seq->current + s;
        if( p < 0 ) p = 0;
        int n = v->seq->samples[ p ].sample_id;
        int t = v->seq->samples[ p ].type;

        if( t == 0 ) {
            if( sample_exists( n ) ) {
                veejay_set_sample(v, n );
            }
        } else {
            if( vj_tag_exists( n ) ) {
		veejay_change_playback_mode(v,VJ_PLAYBACK_MODE_TAG,n);
            }
        }
    }
    if( SAMPLE_PLAYING(v)) {
        int s = (v->settings->current_playback_speed < 0 ? -1 : 1 );
        int n = v->uc->sample_id + s;
        if( sample_exists(n) ) {
            veejay_set_sample(v,n );
        } else {
            n = v->uc->sample_id;
            int stop = sample_highest_valid_id();
            while(!sample_exists(n) ) {
                n += s;
                if( n > stop || n < 1 ) {
                    return;
                }
            }
            veejay_set_sample(v, n );
        }
    }
    else if ( STREAM_PLAYING(v)) {
        int n = v->uc->sample_id + 1;
        if( vj_tag_exists(n) ) {
            veejay_change_playback_mode(v, VJ_PLAYBACK_MODE_TAG, n );
        }
        else {
            n = 1;
            int stop = vj_tag_highest_valid_id();
            while( !vj_tag_exists(n) ) {
                n ++;
                if( n > stop ) {
                    return;
                }
            }
            veejay_change_playback_mode( v, VJ_PLAYBACK_MODE_TAG, n );  
        }
    }
}

void    vj_event_sample_next( void *ptr, const char format[], va_list ap)
{
    veejay_t *v = (veejay_t*) ptr;
    vj_event_sample_next1( v );
}


void    vj_event_sequencer_add_sample(      void *ptr,  const char format[],    va_list ap )
{
    int args[5];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    int seq = args[0];
    int id = args[1];
    int type = args[2];

    if( seq < 0 || seq >= MAX_SEQUENCES )
    {
        veejay_msg( VEEJAY_MSG_ERROR,"Slot not within bounds");
        return;
    }

    if( type == 0 ) {
        if( sample_exists(id ))
        {
            v->seq->samples[seq].sample_id = id;
            v->seq->samples[seq].type = type;
            if( v->seq->size < MAX_SEQUENCES )
                v->seq->size ++;
            veejay_msg(VEEJAY_MSG_INFO, "Added sample %d to slot %d/%d",id, seq,MAX_SEQUENCES );
        }
        else
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Sample %d does not exist. It cannot be added to the sequencer",id);
        }
    }
    else {
        if( vj_tag_exists(id) )
        {
            v->seq->samples[seq].sample_id = id;
            v->seq->samples[seq].type = type;
            if( v->seq->size < MAX_SEQUENCES ) 
                v->seq->size ++;
            veejay_msg(VEEJAY_MSG_INFO, "Added stream %d to slot %d/%d", id, seq, MAX_SEQUENCES );
        }
        else
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Stream %d does not exist. It cannot be added to the sequencer", id );
        }
    }   

}

void    vj_event_sequencer_del_sample(      void *ptr,  const char format[],    va_list ap )
{
    int args[5];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    int seq_it = args[0];

    if( seq_it == -1 ) {
        int i;
        for( i = 0; i < MAX_SEQUENCES; i ++ )
        {
            v->seq->samples[i].sample_id = 0;
            v->seq->samples[i].type = 0;
        }
        v->seq->active = 0;
        v->seq->current = 0;

        veejay_msg(VEEJAY_MSG_INFO, "Deleted all sequences");
        return;
    }

    if( seq_it < 0 || seq_it >= MAX_SEQUENCES )
    {
        veejay_msg( VEEJAY_MSG_ERROR, "Sequence slot %d is not used, nothing deleted",seq_it );
        return;
    }   

    if( v->seq->samples[ seq_it ].sample_id )
    {
        veejay_msg(VEEJAY_MSG_INFO, "Deleted sequence %d (Sample %d)", seq_it, v->seq->samples[ seq_it ].sample_id );
        v->seq->samples[ seq_it ].sample_id = 0;
        v->seq->samples[ seq_it ].type = 0;
    }
    else
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Sequence slot %d already empty", seq_it );
    }

}

void    vj_event_get_sample_sequences(      void *ptr,  const char format[],    va_list ap )
{
    veejay_t *v = (veejay_t*)ptr;
    int i;

    if( v->seq->size <= 0 )
    {
        SEND_MSG(v,"000000");
        return;
    }
    
    char *s_print_buf = get_print_buf( 32  + (MAX_SEQUENCES*6));
    sprintf(s_print_buf, "%06d%04d%04d%04d",
            ( 12 + (6*MAX_SEQUENCES)),
            v->seq->current,MAX_SEQUENCES, v->seq->active );
    
    for( i =0; i < MAX_SEQUENCES ;i ++ )
    {
        char tmp[32];
        sprintf(tmp, "%04d%02d", v->seq->samples[i].sample_id, v->seq->samples[i].type);
        veejay_strncat(s_print_buf, tmp, 6 );
    }

    SEND_MSG(v, s_print_buf );
    free(s_print_buf);  
}

void    vj_event_sample_sequencer_active(   void *ptr,  const char format[],    va_list ap )
{
    int args[5];
    veejay_t *v = (veejay_t*)ptr;
    P_A(args,sizeof(args),NULL,0,format,ap);

    if( v->seq->size == 0 )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Sequencer list is empty. Please add samples first");
        return;
    }

    if( args[0] == 0 )
    {
        v->seq->active = 0;
        v->seq->current = 0;
        vj_perform_reset_transition(v);
    	veejay_reset_sample_positions( v, -1 );
        veejay_msg(VEEJAY_MSG_INFO, "Sample sequencer disabled");
    }
    else 
    {
        v->seq->current = 0;

        int next_type = 0;
        int next_sample_id = vj_perform_get_next_sequence_id(v, &next_type, 0, &(v->seq->current) );
    
        if( next_sample_id > 0 ) {
            v->seq->active = 1;
            veejay_reset_sample_positions( v, -1 );
            veejay_change_playback_mode(v, (next_type == VJ_PLAYBACK_MODE_SAMPLE ? VJ_PLAYBACK_MODE_SAMPLE: VJ_PLAYBACK_MODE_TAG ), next_sample_id );
            veejay_msg(VEEJAY_MSG_INFO, "Sample sequencer enabled");
        }
        else {
            veejay_msg(VEEJAY_MSG_ERROR,"Sample sequencer is empty");
        }
    }
}

static int	vj_event_macro_loop_stat_auto( veejay_t *v, void *macro, int new_state )
{
	int cur_state = vj_macro_get_status( macro );

    int loop_stat_stop = 0;
   
    if(SAMPLE_PLAYING(v)) {
        loop_stat_stop = sample_get_loop_stats(v->uc->sample_id);
    }
    if(STREAM_PLAYING(v)) {
        loop_stat_stop = vj_tag_get_loop_stats(v->uc->sample_id);
    }

	if( cur_state == new_state ) {
		return loop_stat_stop; 
	}

	if( new_state == MACRO_REC ) {
        return 0; // reset loop stat
    }

    if( new_state == MACRO_DESTROY ) {
		return loop_stat_stop;
	}

	if( new_state == MACRO_PLAY ) { // retrieve loop stat for this bank
		loop_stat_stop = vj_macro_get_loop_stat_stop( macro );
	}

	if(SAMPLE_PLAYING(v)) {
		sample_set_loop_stat_stop( v->uc->sample_id, loop_stat_stop );
        sample_set_loop_stats( v->uc->sample_id, 0 ); // reset loop count
	}
	if(STREAM_PLAYING(v)) {
		vj_tag_set_loop_stat_stop( v->uc->sample_id, loop_stat_stop );
		vj_tag_set_loop_stats( v->uc->sample_id, 0 );
	}

	return loop_stat_stop;
}

void    vj_event_set_macro_status( void *ptr,   const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

	void *macro = NULL;

	if( SAMPLE_PLAYING(v) ) {
		macro = sample_get_macro(v->uc->sample_id);
	}
	if( STREAM_PLAYING(v) ) {
		macro = vj_tag_get_macro(v->uc->sample_id);
	}

	if(macro == NULL) {
		return;
	}

	if(args[0] != MACRO_STOP && args[0] != MACRO_REC && args[0] != MACRO_PLAY && args[0] != MACRO_DESTROY) {
		veejay_msg(0, "VIMS macro event rec/play valid states are (%d=STOP,%d=REC,%d=PLAY,%d=DESTROY)", MACRO_STOP,MACRO_REC,MACRO_PLAY,MACRO_DESTROY);
		return;
	}

	int loop_stat_stop = vj_event_macro_loop_stat_auto(v, macro, args[0] );

    if( args[0] == MACRO_STOP )
    {
        vj_macro_set_status( macro, MACRO_STOP );
		veejay_msg(VEEJAY_MSG_INFO, "VIMS Macro is now inactive (loop boundary at %d)", loop_stat_stop);
    }
	else if (args[0] == MACRO_REC)
	{	
		vj_macro_set_status( macro, MACRO_REC );
		veejay_msg(VEEJAY_MSG_INFO, "VIMS Macro recorder is active (loop boundary at %d)", loop_stat_stop);
	}
    else if (args[0] == MACRO_PLAY)
    {
		vj_macro_set_status( macro, MACRO_PLAY);
		veejay_msg(VEEJAY_MSG_INFO, "VIMS Macro playback is active (loop boundary at %d)", loop_stat_stop);
    }
	else if (args[0] == MACRO_DESTROY)
	{
		vj_macro_set_status( macro, MACRO_STOP );
		vj_macro_clear(macro);
        vj_macro_select(macro, -1 );
        vj_macro_set_loop_stat_stop(macro,0);
        veejay_msg(VEEJAY_MSG_INFO, "VIMS Macro is destroyed (loop boundary at %d)", loop_stat_stop);
	}


}

void    vj_event_sample_set_loops       (   void *ptr,  const char format[],     va_list ap )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) {
        SAMPLE_DEFAULTS(args[0]);
        sample_set_loop_stat_stop( args[0], args[1] );
        veejay_msg(VEEJAY_MSG_INFO,"Set loop stop to %d on sample %d",args[1], args[0]);
    }
    else if (STREAM_PLAYING(v)) {
        STREAM_DEFAULTS(args[0]);
        vj_tag_set_loop_stat_stop( args[0], args[1]);
        veejay_msg(VEEJAY_MSG_INFO,"Set loop stop to %d on stream %d",args[1], args[0]);
    }
}

void    vj_event_macro_clear_bank( void *ptr,   const char format[], va_list ap )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[2];
    P_A(args,sizeof(args),NULL,0,format,ap);

    if(SAMPLE_PLAYING(v)) {
		void *macro = sample_get_macro(v->uc->sample_id);
		vj_macro_set_status( macro, MACRO_STOP );
		vj_macro_clear_bank(macro,args[0]);
        vj_macro_set_loop_stat_stop(macro,0);
		veejay_msg(VEEJAY_MSG_INFO, "VIMS Macro bank %d is cleared",args[0]);
	}
    
	if(STREAM_PLAYING(v)) {
		void *macro = vj_tag_get_macro(v->uc->sample_id);
		vj_macro_set_status( macro, MACRO_STOP );
		vj_macro_clear_bank(macro,args[0]);
        vj_macro_set_loop_stat_stop(macro,0);
		veejay_msg(VEEJAY_MSG_INFO, "VIMS Macro bank %d is cleared",args[0]);
	}
}

#define SAMPLE_IMAGE_ERROR "00000000000000000"
void    vj_event_get_sample_image       (   void *ptr,  const char format[],    va_list ap  )
{
    veejay_t *v = (veejay_t*)ptr;
    int args[4];
    
    P_A(args,sizeof(args),NULL,0,format,ap);

    int max_w = vj_perform_preview_max_width(v);
    int max_h = vj_perform_preview_max_height(v);
        
    int w = args[2]; 
    int h = args[3];

    int type = args[1];

    if( type == 0 ) {
        SAMPLE_DEFAULTS(args[0]);
    }
    else {
        STREAM_DEFAULTS(args[0]);
    }

    int id = args[0];

    int startFrame = (type == 0 ? 0 : -1);

    if( w <= 0 || h <= 0 || w >= max_w || h >= max_h )
    {
        veejay_msg(0, "Invalid image dimension %dx%d requested (max is %dx%d)",w,h,max_w,max_h );
        SEND_MSG(v, SAMPLE_IMAGE_ERROR );
        return;
    }

    int dstlen = 0;
    
    editlist *el = ( type == VJ_PLAYBACK_MODE_SAMPLE ? sample_get_editlist(id) : v->edit_list );
    if( el == NULL && type == VJ_PLAYBACK_MODE_SAMPLE ) {
        veejay_msg(0, "No such sample %d", id );
        SEND_MSG(v, SAMPLE_IMAGE_ERROR );
        return;
    }

    uint8_t *img[4] = { 0 };
    if( sample_image_buffer == NULL ) {
        sample_image_buffer = (uint8_t*) vj_malloc( sizeof(uint8_t) * v->video_output_width * v->video_output_height * 4);
    }
    if( sample_image_buffer == NULL ) {
        veejay_msg(0, "Not enough memory", id );
        SEND_MSG(v, SAMPLE_IMAGE_ERROR );
        return;
    }

    img[0] = sample_image_buffer;
    img[1] = img[0] + v->video_output_width * v->video_output_height;
    img[2] = img[1] + v->video_output_width * v->video_output_height;

    if( startFrame >= 0 ) {
        int ret = vj_el_get_video_frame( el, startFrame, img );
        if( ret == 0 ) {
            veejay_msg(VEEJAY_MSG_WARNING,"Unable to decode frame %ld", startFrame);
            SEND_MSG(v, SAMPLE_IMAGE_ERROR );
            return;
        }
    }
    else {
        SEND_MSG(v, SAMPLE_IMAGE_ERROR );
        return;
    }   

    VJFrame *frame = yuv_yuv_template( img[0],img[1],img[2], v->video_output_width, v->video_output_height,
            get_ffmpeg_pixfmt( v->pixel_format ));

    if( use_bw_preview_ ) {
        vj_fastbw_picture_save_to_mem(
                frame,
                w,
                h,
                vj_perform_get_preview_buffer(v));
        dstlen = w * h;
    }
    else {
        vj_fast_picture_save_to_mem(
                frame,
                w,
                h,
                vj_perform_get_preview_buffer(v) );
        dstlen = (w * h) + ((w*h)/4) + ((w*h)/4);
    }

    char header[32];
    snprintf( header,sizeof(header), "%06d%04d%2d%1d", dstlen, args[0],args[1], yuv_get_pixel_range() );
    SEND_DATA(v, header, 13 );
    SEND_DATA(v, vj_perform_get_preview_buffer(v), dstlen );

    free(frame);

}


void vj_event_alpha_composite(void *ptr, const char format[], va_list ap) 
{
    veejay_t *v = (veejay_t*) ptr;
    int args[4];
    
    P_A(args,sizeof(args), NULL,0,format,ap);

    if( args[0] == 0 ) {
        v->settings->clear_alpha = 0;
        v->settings->alpha_value = args[1];
        if(v->settings->alpha_value < 0 )
            v->settings->alpha_value = 0;
        else if (v->settings->alpha_value > 255 )
            v->settings->alpha_value = 255;
        veejay_msg(VEEJAY_MSG_INFO,"Enabled alpha channel leaking (no clear)");
    } else if (args[0] == 1 ) {
        v->settings->clear_alpha = 1;
        v->settings->alpha_value = args[1];
        if(v->settings->alpha_value < 0 )
            v->settings->alpha_value = 0;
        else if (v->settings->alpha_value > 255 )
            v->settings->alpha_value = 255;
        veejay_msg(VEEJAY_MSG_INFO,"New alpha mask every frame (default alpha value is %d)", v->settings->alpha_value);
    }
}

