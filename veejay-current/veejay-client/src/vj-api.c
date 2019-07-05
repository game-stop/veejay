/* Gveejay Reloaded - graphical interface for VeeJay
 *          (C) 2002-2004 Niels Elburg <nwelburg@gmail.com>
 *  with contributions by  Thomas Rheinhold (2005)
 *                        (initial sampledeck representation in GTK)
 *  with contributions by  Jerome Blanchi (2016-2018)
 *                        (Gtk3 Migration and other stuff)
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
#include <math.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sysexits.h>
#include <unistd.h>
#include <dirent.h>
#include <stdarg.h>
#include <glib.h>
#include <errno.h>
#include <libavutil/avutil.h>
#include <libavutil/pixfmt.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <string.h>
#include <sys/time.h>
#include <stdint.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/defs.h>
#include <veejaycore/vj-client.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <veejaycore/avcommon.h>
#include <veejaycore/vevo.h>
#include <src/vj-api.h>
#include <fcntl.h>
#include <veejaycore/mjpeg_logging.h>
#include <veejaycore/yuv4mpeg.h>
#include <veejaycore/mpegconsts.h>
#include <veejaycore/mpegtimecode.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cellrendererspin.h>
#include <gtktimeselection.h>
#include <libgen.h>
#ifdef HAVE_SDL
#include <src/keyboard.h>
#endif
#include <gtk3curve.h>
#include <src/curve.h>
#include <src/multitrack.h>
#include <src/common.h>
#include <src/utils.h>
#include <src/sequence.h>
#include <veejaycore/yuvconv.h>
#include <veejaycore/libvevo.h>
#include <src/vmidi.h>
#include <src/utils-gtk.h>

#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#define RUP8(num)(((num)+8)&~8)
#ifdef ARCH_X86_64
static gpointer castIntToGpointer( int val )
{
    int64_t g_int64_val = (int64_t) val;
    return (gpointer) g_int64_val;
}
#else
static gpointer castIntToGpointer( int val)
{
    return (gpointer) val;
}
#endif

#define MAX_SLOW 25
#define QUICKSELECT_SLOTS 10
#define MAX_WIDGET_CACHE 512

static int beta__ = 0;
static int use_vims_mcast = 0;
static int samplebank_ready_ = 0;
static int faster_ui_ = 0;

static GtkWidget *widget_cache[MAX_WIDGET_CACHE];

enum {
  WIDGET_IMAGEA = 0,
  WIDGET_NOTEBOOK18 = 1,
  WIDGET_LABEL_CURFRAME = 2,
  WIDGET_LABEL_MOUSEAT = 3,
  WIDGET_LABEL_CURTIME = 4,
  WIDGET_LABEL_SAMPLEPOSITION = 5,
  WIDGET_PANELS = 6,
  WIDGET_VIMS_MESSENGER_PLAY = 7,
  WIDGET_STATUSBAR = 8,
  WIDGET_LOOP_NONE = 9,
  WIDGET_LOOP_NORMAL = 10,
  WIDGET_LOOP_PINGPONG = 11,
  WIDGET_LOOP_RANDOM = 12,
  WIDGET_LOOP_ONCENOP = 13,
  WIDGET_LABEL_MARKEREND = 14,
  WIDGET_TOGGLE_SUBRENDER = 15,
  WIDGET_TOGGLE_FADEMETHOD = 16,
  WIDGET_SPIN_SAMPLESPEED = 17,
  WIDGET_SPIN_SAMPLESTART = 18,
  WIDGET_SPIN_SAMPLEEND = 19,
  WIDGET_PLAYHINT = 20,
  WIDGET_LABEL_MARKERDURATION = 21,
  WIDGET_LABEL_MARKERSTART = 22,
  WIDGET_SPEED_SLIDER = 23,
  WIDGET_SLOW_SLIDER = 24,
  WIDGET_SPIN_TEXT_START = 25,
  WIDGET_SPIN_TEXT_END = 26,
  WIDGET_MANUALOPACITY = 27,
  WIDGET_SAMPLERAND = 28,
  WIDGET_STREAM_LENGTH = 29,
  WIDGET_STREAM_LENGTH_LABEL = 30,
  WIDGET_BUTTON_FADEDUR = 31,
  WIDGET_LABEL_TOTFRAMES = 32,
  WIDGET_LABEL_SAMPLELENGTH = 33,
  WIDGET_LABEL_TOTALTIME = 34,
  WIDGET_LABEL_SAMPLEPOS = 35,
  WIDGET_FEEDBACKBUTTON = 36,
  WIDGET_MACRORECORD = 37,
  WIDGET_MACROPLAY = 38,
  WIDGET_MACROSTOP = 39,
  WIDGET_BUTTON_EL_SELSTART = 40,
  WIDGET_BUTTON_EL_SELEND = 41,
  WIDGET_LABEL_LOOP_STAT_STOP = 42,
  WIDGET_SAMPLE_LOOPSTOP = 43,
  WIDGET_STREAM_LOOPSTOP = 44,
  WIDGET_LABEL_LOOP_STATS = 45,
  WIDGET_SEQACTIVE = 46,
  WIDGET_LABEL_CURRENTID = 47,
  WIDGET_CALI_TAKE_BUTTON = 48,
  WIDGET_CURRENT_STEP_LABEL = 49,
  WIDGET_LABEL_EFFECTNAME = 50,
  WIDGET_LABEL_EFFECTANIM_NAME = 51,
  WIDGET_VALUE_FRIENDLYNAME = 52,
  WIDGET_BUTTON_ENTRY_TOGGLE = 53,
  WIDGET_SUBRENDER_ENTRY_TOGGLE = 54,
  WIDGET_TRANSITION_LOOP = 55,
  WIDGET_TRANSITION_ENABLED = 56,
  WIDGET_COMBO_CURVE_FX_PARAM = 57,
  WIDGET_FX_M1 = 58,
  WIDGET_FX_M2 = 59,
  WIDGET_FX_M3 = 60,
  WIDGET_FX_M4 = 61,
  WIDGET_CHECK_SAMPLEFX = 62,
  WIDGET_CHECK_STREAMFX = 63,
  WIDGET_SLIDER_P0 = 64,
  WIDGET_SLIDER_P1 = 65,
  WIDGET_SLIDER_P2 = 66,
  WIDGET_SLIDER_P3 = 67,
  WIDGET_SLIDER_P4 = 68,
  WIDGET_SLIDER_P5 = 69,
  WIDGET_SLIDER_P6 = 70,
  WIDGET_SLIDER_P7 = 71,
  WIDGET_SLIDER_P8 = 72,
  WIDGET_SLIDER_P9 = 73,
  WIDGET_SLIDER_P10 = 74,
  WIDGET_SLIDER_P11 = 75,
  WIDGET_SLIDER_P12 = 76,
  WIDGET_SLIDER_P13 = 77,
  WIDGET_SLIDER_P14 = 78,
  WIDGET_SLIDER_P15 = 79,
  WIDGET_RGBKEY = 80,
  WIDGET_FRAME_P0 = 81,
  WIDGET_FRAME_P1 = 82,
  WIDGET_FRAME_P2 = 83,
  WIDGET_FRAME_P3 = 84,
  WIDGET_FRAME_P4 = 85,
  WIDGET_FRAME_P5 = 86,
  WIDGET_FRAME_P6 = 87,
  WIDGET_FRAME_P7 = 88,
  WIDGET_FRAME_P8 = 89,
  WIDGET_FRAME_P9 = 90,
  WIDGET_FRAME_P10 = 91,
  WIDGET_FRAME_P11 = 92,
  WIDGET_FRAME_P12 = 93,
  WIDGET_FRAME_P13 = 94,
  WIDGET_FRAME_P14 = 95,
  WIDGET_FRAME_P15 = 96,
  WIDGET_SLIDER_BOX_P0 = 97,
  WIDGET_SLIDER_BOX_P1 = 98,
  WIDGET_SLIDER_BOX_P2 = 99,
  WIDGET_SLIDER_BOX_P3 = 100,
  WIDGET_SLIDER_BOX_P4 = 101,
  WIDGET_SLIDER_BOX_P5 = 102,
  WIDGET_SLIDER_BOX_P6 = 103,
  WIDGET_SLIDER_BOX_P7 = 104,
  WIDGET_SLIDER_BOX_P8 = 105,
  WIDGET_SLIDER_BOX_P9 = 106,
  WIDGET_SLIDER_BOX_P10 = 107,
  WIDGET_SLIDER_BOX_P11 = 108,
  WIDGET_SLIDER_BOX_P12 = 109,
  WIDGET_SLIDER_BOX_P13 = 110,
  WIDGET_SLIDER_BOX_P14 = 111,
  WIDGET_SLIDER_BOX_P15 = 112,
  WIDGET_LABEL_P0 = 113,
  WIDGET_LABEL_P1 = 114,
  WIDGET_LABEL_P2 = 115,
  WIDGET_LABEL_P3 = 116,
  WIDGET_LABEL_P4 = 117,
  WIDGET_LABEL_P5 = 118,
  WIDGET_LABEL_P6 = 119,
  WIDGET_LABEL_P7 = 120,
  WIDGET_LABEL_P8 = 121,
  WIDGET_LABEL_P9 = 122,
  WIDGET_LABEL_P10 = 123,
  WIDGET_LABEL_P11 = 124,
  WIDGET_LABEL_P12 = 125,
  WIDGET_LABEL_P13 = 126,
  WIDGET_LABEL_P14 = 127,
  WIDGET_LABEL_P15 = 128,
  WIDGET_INC_P0 = 129,
  WIDGET_INC_P1 = 130,
  WIDGET_INC_P2 = 131,
  WIDGET_INC_P3 = 132,
  WIDGET_INC_P4 = 133,
  WIDGET_INC_P5 = 134,
  WIDGET_INC_P6 = 135,
  WIDGET_INC_P7 = 136,
  WIDGET_INC_P8 = 137,
  WIDGET_INC_P9 = 138,
  WIDGET_INC_P10 = 139,
  WIDGET_INC_P11 = 140,
  WIDGET_INC_P12 = 141,
  WIDGET_INC_P13 = 142,
  WIDGET_INC_P14 = 143,
  WIDGET_INC_P15 = 144,
  WIDGET_DEC_P0 = 145,
  WIDGET_DEC_P1 = 146,
  WIDGET_DEC_P2 = 147,
  WIDGET_DEC_P3 = 148,
  WIDGET_DEC_P4 = 149,
  WIDGET_DEC_P5 = 150,
  WIDGET_DEC_P6 = 151,
  WIDGET_DEC_P7 = 152,
  WIDGET_DEC_P8 = 153,
  WIDGET_DEC_P9 = 154,
  WIDGET_DEC_P10 = 155,
  WIDGET_DEC_P11 = 156,
  WIDGET_DEC_P12 = 157,
  WIDGET_DEC_P13 = 158,
  WIDGET_DEC_P14 = 159,
  WIDGET_DEC_P15 = 160,
  WIDGET_FRAME_FXTREE2 = 161,
  WIDGET_CURVE_TOGGLEENTRY_PARAM = 163,
  WIDGET_CURVE_SPINSTART = 164,
  WIDGET_CURVE_SPINEND = 165,
  WIDGET_CURVE_CHAIN_TOGGLEENTRY = 166,
  WIDGET_FRAME_FXTREE3 = 167,
  WIDGET_CURVE_TYPESPLINE = 168,
  WIDGET_CURVE_TYPEFREEHAND = 169,
  WIDGET_CURVE_TYPELINEAR = 170,
  WIDGET_SAMPLE_LENGTH_LABEL = 171,
  WIDGET_BUTTON_FX_ENTRY = 172,
  WIDGET_NOTEBOOK15 = 173,
  WIDGET_SAMPLE_LOOP_BOX = 174,
  WIDGET_BUTTON_084 = 175,
  WIDGET_VIDEO_NAVIGATION_BUTTONS = 176,
  WIDGET_BUTTON_SAMPLESTART = 177,
  WIDGET_BUTTON_SAMPLEEND = 178,
  WIDGET_BUTTON_200 = 181,
  WIDGET_FRAME_FXTREE = 182,
  WIDGET_VJFRAMERATE = 183,
  WIDGET_SCROLLEDWINDOW49 = 184,
  WIDGET_SAMPLEGRID_FRAME = 185,
  WIDGET_MARKERFRAME = 186,
  WIDGET_FXPANEL = 187,
  WIDGET_BUTTON_083 = 188,
  WIDGET_FXANIMCONTROLS = 189,
  WIDGET_CURVECONTAINER = 190,
  WIDGET_SRTFRAME = 192,
  WIDGET_FRAME_FXTREE1 = 193,
  WIDGET_BUTTON_5_4 = 194,
  WIDGET_TOGGLE_MULTICAST = 195,
  WIDGET_CALI_SAVE_BUTTON = 196,
  WIDGET_VBOX633 = 197,
  WIDGET_HBOX910 = 198,
  WIDGET_HBOX27 = 199,
  WIDGET_SAMPLE_BANK_HBOX = 200,
  WIDGET_BUTTON_SAMPLEBANK_PREV = 201,
  WIDGET_BUTTON_SAMPLEBANK_NEXT = 202,
  WIDGET_SPIN_SAMPLEBANK_SELECT = 203,
  WIDGET_HBOX709 = 204,
  WIDGET_SAMPLE_PANEL = 205,
  WIDGET_VBOX623 = 206,
  WIDGET_SLIDER_BOX_G0 = 207,
  WIDGET_SLIDER_BOX_G1 = 208,
  WIDGET_SLIDER_BOX_G2 = 209,
  WIDGET_SLIDER_BOX_G3 = 210,
  WIDGET_SLIDER_BOX_G4 = 211,
  WIDGET_SLIDER_BOX_G5 = 212,
  WIDGET_SLIDER_BOX_G6 = 213,
  WIDGET_SLIDER_BOX_G7 = 214,
  WIDGET_SLIDER_BOX_G8 = 215,
  WIDGET_SLIDER_BOX_G9 = 216,
  WIDGET_SLIDER_BOX_G10 = 217,
  WIDGET_VEEJAY_BOX = 230,
  WIDGET_CURVE_CHAIN_TOGGLECHAIN = 231,
  WIDGET_FX_MNONE = 232,
  WIDGET_SPIN_MACRODELAY = 233,
  WIDGET_MACRORECORD1 = 234,
  WIDGET_MACROPLAY1 = 235,
  WIDGET_MACROSTOP1 = 236,
  WIDGET_SPIN_SAMPLEDURATION = 237,
};


enum {
 SAMPLE_WIDGET_SAMPLE_LOOP_BOX = 0,
 SAMPLE_WIDGET_BUTTON_084,
 SAMPLE_WIDGET_VIDEO_NAVIGATION_BUTTONS,
 SAMPLE_WIDGET_BUTTON_SAMPLESTART,
 SAMPLE_WIDGET_BUTTON_SAMPLEEND,
 SAMPLE_WIDGET_SPEED_SLIDER,
 SAMPLE_WIDGET_SLOW_SLIDER,
 SAMPLE_WIDGET_BUTTON_200,
 SAMPLE_WIDGET_FRAME_FXTREE,
 SAMPLE_WIDGET_VJFRAMERATE,
 SAMPLE_WIDGET_SCROLLEDWINDOW49,
 SAMPLE_WIDGET_SAMPLEGRID_FRAME,
 SAMPLE_WIDGET_MARKERFRAME,
 SAMPLE_WIDGET_FXPANEL,
 SAMPLE_WIDGET_BUTTON_083,
 SAMPLE_WIDGET_PANEL,
 SAMPLE_WIDGET_NONE // must be last
};

enum {
  PLAIN_WIDGET_VIDEO_NAVIGATION_BUTTONS = 0,
  PLAIN_WIDGET_BUTTON_084,
  PLAIN_WIDGET_BUTTON_083,
  PLAIN_WIDGET_BUTTON_SAMPLESTART,
  PLAIN_WIDGET_BUTTON_SAMPLEEND,
  PLAIN_WIDGET_SPEED_SLIDER,
  PLAIN_WIDGET_SLOW_SLIDER,
  PLAIN_WIDGET_VJFRAMERATE,
  PLAIN_WIDGET_MARKERFRAME,
  PLAIN_WIDGET_NONE,
};

enum {
  STREAM_WIDGET_BUTTON_200 = 0,
  STREAM_WIDGET_FRAME_FXTREE,
  STREAM_WIDGET_FRAME_FXTREE3,
  STREAM_WIDGET_FXPANEL,
  STREAM_WIDGET_PANEL,
  STREAM_WIDGET_SCROLLEDWINDOW49,
  STREAM_WIDGET_SAMPLEGRID_FRAME,
  STREAM_WIDGET_NONE,
};

enum {
  FB_WIDGET_MARKERFRAME = 0,
  FB_WIDGET_VBOX633,
  FB_WIDGET_HBOX910,
  FB_WIDGET_HBOX27,
  FB_WIDGET_SAMPLE_BANK_HBOX,
  FB_WIDGET_BUTTON_SAMPLEBANK_PREV,
  FB_WIDGET_BUTTON_SAMPLEBANK_NEXT,
  FB_WIDGET_SPIN_SAMPLEBANK_SELECT,
  FB_WIDGET_HBOX709,
  FB_WIDGET_SAMPLE_PANEL,
  FB_WIDGET_NOTEBOOK15,
  FB_WIDGET_VBOX623,
  FB_WIDGET_SAMPLEGRID_FRAME,
  FB_WIDGET_PANELS,
  FB_WIDGET_NONE
};

static struct 
{
    const int fb_widget_id;
    const int widget_id;
} fb_widget_map[] = 
{
    {  FB_WIDGET_MARKERFRAME, WIDGET_MARKERFRAME },
    {  FB_WIDGET_VBOX633, WIDGET_VBOX633 },
    {  FB_WIDGET_HBOX910, WIDGET_HBOX910 },
    {  FB_WIDGET_HBOX27, WIDGET_HBOX27 },
    {  FB_WIDGET_SAMPLE_BANK_HBOX, WIDGET_SAMPLE_BANK_HBOX },
    {  FB_WIDGET_BUTTON_SAMPLEBANK_PREV, WIDGET_BUTTON_SAMPLEBANK_PREV },
    {  FB_WIDGET_BUTTON_SAMPLEBANK_NEXT, WIDGET_BUTTON_SAMPLEBANK_NEXT },
    {  FB_WIDGET_SPIN_SAMPLEBANK_SELECT, WIDGET_SPIN_SAMPLEBANK_SELECT },
    {  FB_WIDGET_HBOX709,WIDGET_HBOX709 },
    {  FB_WIDGET_SAMPLE_PANEL, WIDGET_SAMPLE_PANEL },
    {  FB_WIDGET_NOTEBOOK15, WIDGET_NOTEBOOK15 },
    {  FB_WIDGET_VBOX623, WIDGET_VBOX623 },
    {  FB_WIDGET_SAMPLEGRID_FRAME,FB_WIDGET_SAMPLEGRID_FRAME },
    {  FB_WIDGET_PANELS, WIDGET_PANELS },
};


static struct 
{
    const int stream_widget_id;
    const int widget_id;
} 
stream_widget_map[] = 
{
    { STREAM_WIDGET_BUTTON_200,           WIDGET_BUTTON_200 },
    { STREAM_WIDGET_FRAME_FXTREE,         WIDGET_FRAME_FXTREE },
    { STREAM_WIDGET_FRAME_FXTREE3,        WIDGET_FRAME_FXTREE3 },
    { STREAM_WIDGET_FXPANEL,              WIDGET_FXPANEL },
    { STREAM_WIDGET_PANEL,                WIDGET_PANELS },
    { STREAM_WIDGET_SCROLLEDWINDOW49,     WIDGET_SCROLLEDWINDOW49 },
    { STREAM_WIDGET_SAMPLEGRID_FRAME,     WIDGET_SAMPLEGRID_FRAME },
    { STREAM_WIDGET_NONE, -1 },
};




static struct 
{
    const int sample_widget_id;
    const int widget_id;
} sample_widget_map[] = 
{
    {  SAMPLE_WIDGET_SAMPLE_LOOP_BOX,           WIDGET_SAMPLE_LOOP_BOX },
    {  SAMPLE_WIDGET_BUTTON_084,                WIDGET_BUTTON_084 },
    {  SAMPLE_WIDGET_VIDEO_NAVIGATION_BUTTONS,  WIDGET_VIDEO_NAVIGATION_BUTTONS },
    {  SAMPLE_WIDGET_BUTTON_SAMPLESTART,        WIDGET_BUTTON_SAMPLESTART },
    {  SAMPLE_WIDGET_BUTTON_SAMPLEEND,          WIDGET_BUTTON_SAMPLEEND },
    {  SAMPLE_WIDGET_SPEED_SLIDER,              WIDGET_SPEED_SLIDER },
    {  SAMPLE_WIDGET_SLOW_SLIDER,               WIDGET_SLOW_SLIDER },
    {  SAMPLE_WIDGET_BUTTON_200,                WIDGET_BUTTON_200 },
    {  SAMPLE_WIDGET_FRAME_FXTREE,              WIDGET_FRAME_FXTREE },
    {  SAMPLE_WIDGET_VJFRAMERATE,               WIDGET_VJFRAMERATE },
    {  SAMPLE_WIDGET_SCROLLEDWINDOW49,          WIDGET_SCROLLEDWINDOW49 },
    {  SAMPLE_WIDGET_SAMPLEGRID_FRAME,          WIDGET_SAMPLEGRID_FRAME },
    {  SAMPLE_WIDGET_MARKERFRAME,               WIDGET_MARKERFRAME },
    {  SAMPLE_WIDGET_FXPANEL,                   WIDGET_FXPANEL },
    {  SAMPLE_WIDGET_BUTTON_083,                WIDGET_BUTTON_083 },
    {  SAMPLE_WIDGET_PANEL,                     WIDGET_PANELS },
    {  SAMPLE_WIDGET_NONE,                      -1 },
};

static struct 
{
    const int plain_widget_id;
    const int widget_id;
} plain_widget_map[] = 
{
    { PLAIN_WIDGET_VIDEO_NAVIGATION_BUTTONS,    WIDGET_VIDEO_NAVIGATION_BUTTONS },
    { PLAIN_WIDGET_BUTTON_084,                  WIDGET_BUTTON_084 },
    { PLAIN_WIDGET_BUTTON_083,                  WIDGET_BUTTON_083 },
    { PLAIN_WIDGET_BUTTON_SAMPLESTART,          WIDGET_BUTTON_SAMPLESTART },
    { PLAIN_WIDGET_BUTTON_SAMPLEEND,            WIDGET_BUTTON_SAMPLEEND },
    { PLAIN_WIDGET_SPEED_SLIDER,                WIDGET_SPEED_SLIDER },
    { PLAIN_WIDGET_SLOW_SLIDER,                 WIDGET_SLOW_SLIDER },
    { PLAIN_WIDGET_VJFRAMERATE,                 WIDGET_VJFRAMERATE },
    { PLAIN_WIDGET_MARKERFRAME,                 WIDGET_MARKERFRAME },
    { PLAIN_WIDGET_NONE,                        -1 },
};

static struct
{
    const char *name;
    const int id;
} widget_map[] = 
{
    { "imageA",                 WIDGET_IMAGEA },
    { "notebook18",             WIDGET_NOTEBOOK18 },
    { "panels",                 WIDGET_PANELS },
    { "notebook15",             WIDGET_NOTEBOOK15 },
    { "label_curframe",         WIDGET_LABEL_CURFRAME },
    { "label_mouseat",          WIDGET_LABEL_MOUSEAT },
    { "label_curtime",          WIDGET_LABEL_CURTIME },
    { "label_sampleposition",   WIDGET_LABEL_SAMPLEPOSITION },
    { "vims_messenger_play",    WIDGET_VIMS_MESSENGER_PLAY },
    { "statusbar",              WIDGET_STATUSBAR },
    { "loop_none",              WIDGET_LOOP_NONE },
    { "loop_normal",            WIDGET_LOOP_NORMAL },
    { "loop_pingpong",          WIDGET_LOOP_PINGPONG },
    { "loop_random",            WIDGET_LOOP_RANDOM },
    { "loop_oncenop",           WIDGET_LOOP_ONCENOP },
    { "label_markerend",        WIDGET_LABEL_MARKEREND },
    { "toggle_subrender",       WIDGET_TOGGLE_SUBRENDER },
    { "toggle_fademethod",      WIDGET_TOGGLE_FADEMETHOD },
    { "spin_samplespeed",       WIDGET_SPIN_SAMPLESPEED },
    { "spin_samplestart",       WIDGET_SPIN_SAMPLESTART },
    { "spin_sampleend",         WIDGET_SPIN_SAMPLEEND },
    { "playhint",               WIDGET_PLAYHINT },
    { "label_markerduration",   WIDGET_LABEL_MARKERDURATION },
    { "label_markerstart",      WIDGET_LABEL_MARKERSTART },
    { "speed_slider",           WIDGET_SPEED_SLIDER },
    { "slow_slider",            WIDGET_SLOW_SLIDER },
    { "spin_text_start",        WIDGET_SPIN_TEXT_START },
    { "spin_text_end",          WIDGET_SPIN_TEXT_END },
    { "manualopacity",          WIDGET_MANUALOPACITY },
    { "samplerand",             WIDGET_SAMPLERAND },
    { "stream_length",          WIDGET_STREAM_LENGTH },
    { "stream_length_label",    WIDGET_STREAM_LENGTH_LABEL },
    { "button_fadedur",         WIDGET_BUTTON_FADEDUR },
    { "label_totframes",        WIDGET_LABEL_TOTFRAMES },
    { "label_samplelength",     WIDGET_LABEL_SAMPLELENGTH },
    { "label_totaltime",        WIDGET_LABEL_TOTALTIME },
    { "sample_length_label",    WIDGET_SAMPLE_LENGTH_LABEL },
    { "label_samplepos",        WIDGET_LABEL_SAMPLEPOS },
    { "feedbackbutton",         WIDGET_FEEDBACKBUTTON },
    { "macrorecord",            WIDGET_MACRORECORD },
    { "macroplay",              WIDGET_MACROPLAY },
    { "macrostop",              WIDGET_MACROSTOP },
    { "button_el_selstart",     WIDGET_BUTTON_EL_SELSTART },
    { "button_el_selend",       WIDGET_BUTTON_EL_SELEND },
    { "label_loop_stat_stop",   WIDGET_LABEL_LOOP_STAT_STOP },
    { "sample_loopstop",        WIDGET_SAMPLE_LOOPSTOP },
    { "stream_loopstop",        WIDGET_STREAM_LOOPSTOP },
    { "label_loop_stats",       WIDGET_LABEL_LOOP_STATS },
    { "seqactive",              WIDGET_SEQACTIVE },
    { "label_currentid",        WIDGET_LABEL_CURRENTID },
    { "cali_take_button",       WIDGET_CALI_TAKE_BUTTON },
    { "current_step_label",     WIDGET_CURRENT_STEP_LABEL },
    { "check_samplefx",         WIDGET_CHECK_SAMPLEFX },
    { "check_streamfx",         WIDGET_CHECK_STREAMFX },
    { "slider_p0",              WIDGET_SLIDER_P0 },
    { "slider_p1",              WIDGET_SLIDER_P1 },
    { "slider_p2",              WIDGET_SLIDER_P2 },
    { "slider_p3",              WIDGET_SLIDER_P3 },
    { "slider_p4",              WIDGET_SLIDER_P4 },
    { "slider_p5",              WIDGET_SLIDER_P5 },
    { "slider_p6",              WIDGET_SLIDER_P6 },
    { "slider_p7",              WIDGET_SLIDER_P7 },
    { "slider_p8",              WIDGET_SLIDER_P8 },
    { "slider_p9",              WIDGET_SLIDER_P9 },
    { "slider_p10",             WIDGET_SLIDER_P10 },
    { "slider_p11",             WIDGET_SLIDER_P11 },
    { "slider_p12",             WIDGET_SLIDER_P12 },
    { "slider_p13",             WIDGET_SLIDER_P13 },
    { "slider_p14",             WIDGET_SLIDER_P14 },
    { "slider_p15",             WIDGET_SLIDER_P15 },
    { "frame_p0",               WIDGET_FRAME_P0 },
    { "frame_p1",               WIDGET_FRAME_P1 },
    { "frame_p2",               WIDGET_FRAME_P2 },
    { "frame_p3",               WIDGET_FRAME_P3 },
    { "frame_p4",               WIDGET_FRAME_P4 },
    { "frame_p5",               WIDGET_FRAME_P5 },
    { "frame_p6",               WIDGET_FRAME_P6 },
    { "frame_p7",               WIDGET_FRAME_P7 },
    { "frame_p8",               WIDGET_FRAME_P8 },
    { "frame_p9",               WIDGET_FRAME_P9 },
    { "frame_p10",              WIDGET_FRAME_P10 },
    { "frame_p11",              WIDGET_FRAME_P11 },
    { "frame_p12",              WIDGET_FRAME_P12 },
    { "frame_p13",              WIDGET_FRAME_P13 },
    { "frame_p14",              WIDGET_FRAME_P14 },
    { "frame_p15",              WIDGET_FRAME_P15 },
    { "slider_box_p0",          WIDGET_SLIDER_BOX_P0 },
    { "slider_box_p1",          WIDGET_SLIDER_BOX_P1 },
    { "slider_box_p2",          WIDGET_SLIDER_BOX_P2 },
    { "slider_box_p3",          WIDGET_SLIDER_BOX_P3 },
    { "slider_box_p4",          WIDGET_SLIDER_BOX_P4 },
    { "slider_box_p5",          WIDGET_SLIDER_BOX_P5 },
    { "slider_box_p6",          WIDGET_SLIDER_BOX_P6 },
    { "slider_box_p7",          WIDGET_SLIDER_BOX_P7 },
    { "slider_box_p8",          WIDGET_SLIDER_BOX_P8 },
    { "slider_box_p9",          WIDGET_SLIDER_BOX_P9 },
    { "slider_box_p10",         WIDGET_SLIDER_BOX_P10 },
    { "slider_box_p11",         WIDGET_SLIDER_BOX_P11 },
    { "slider_box_p12",         WIDGET_SLIDER_BOX_P12 },
    { "slider_box_p13",         WIDGET_SLIDER_BOX_P13 },
    { "slider_box_p14",         WIDGET_SLIDER_BOX_P14 },
    { "slider_box_p15",         WIDGET_SLIDER_BOX_P15 },
    { "label_p0",               WIDGET_LABEL_P0 },
    { "label_p1",               WIDGET_LABEL_P1 },
    { "label_p2",               WIDGET_LABEL_P2 },
    { "label_p3",               WIDGET_LABEL_P3 },
    { "label_p4",               WIDGET_LABEL_P4 },
    { "label_p5",               WIDGET_LABEL_P5 },
    { "label_p6",               WIDGET_LABEL_P6 },
    { "label_p7",               WIDGET_LABEL_P7 },
    { "label_p8",               WIDGET_LABEL_P8 },
    { "label_p9",               WIDGET_LABEL_P9 },
    { "label_p10",              WIDGET_LABEL_P10 },
    { "label_p11",              WIDGET_LABEL_P11 },
    { "label_p12",              WIDGET_LABEL_P12 },
    { "label_p13",              WIDGET_LABEL_P13 },
    { "label_p14",              WIDGET_LABEL_P14 },
    { "label_p15",              WIDGET_LABEL_P15 },
    { "inc_p0",                 WIDGET_INC_P0 },
    { "inc_p1",                 WIDGET_INC_P1 },
    { "inc_p2",                 WIDGET_INC_P2 },
    { "inc_p3",                 WIDGET_INC_P3 },
    { "inc_p4",                 WIDGET_INC_P4 },
    { "inc_p5",                 WIDGET_INC_P5 },
    { "inc_p6",                 WIDGET_INC_P6 },
    { "inc_p7",                 WIDGET_INC_P7 },
    { "inc_p8",                 WIDGET_INC_P8 },
    { "inc_p9",                 WIDGET_INC_P9 },
    { "inc_p10",                WIDGET_INC_P10 },
    { "inc_p11",                WIDGET_INC_P11 },
    { "inc_p12",                WIDGET_INC_P12 },
    { "inc_p13",                WIDGET_INC_P13 },
    { "inc_p14",                WIDGET_INC_P14 },
    { "inc_p15",                WIDGET_INC_P15 },
    { "dec_p0",                 WIDGET_DEC_P0 },
    { "dec_p1",                 WIDGET_DEC_P1 },
    { "dec_p2",                 WIDGET_DEC_P2 },
    { "dec_p3",                 WIDGET_DEC_P3 },
    { "dec_p4",                 WIDGET_DEC_P4 },
    { "dec_p5",                 WIDGET_DEC_P5 },
    { "dec_p6",                 WIDGET_DEC_P6 },
    { "dec_p7",                 WIDGET_DEC_P7 },
    { "dec_p8",                 WIDGET_DEC_P8 },
    { "dec_p9",                 WIDGET_DEC_P9 },
    { "dec_p10",                WIDGET_DEC_P10 },
    { "dec_p11",                WIDGET_DEC_P11 },
    { "dec_p12",                WIDGET_DEC_P12 },
    { "dec_p13",                WIDGET_DEC_P13 },
    { "dec_p14",                WIDGET_DEC_P14 },
    { "dec_p15",                WIDGET_DEC_P15 },
    { "rgbkey",                 WIDGET_RGBKEY },
    { "frame_fxtree2",          WIDGET_FRAME_FXTREE2 },
    { "curve_toggleentry_param",WIDGET_CURVE_TOGGLEENTRY_PARAM },
    { "curve_spinstart",        WIDGET_CURVE_SPINSTART },
    { "curve_spinend",          WIDGET_CURVE_SPINEND },
    { "curve_chain_toggleentry",WIDGET_CURVE_CHAIN_TOGGLEENTRY },
    { "frame_fxtree3",          WIDGET_FRAME_FXTREE3 },
    { "curve_typespline",       WIDGET_CURVE_TYPESPLINE },
    { "curve_typefreehand",     WIDGET_CURVE_TYPEFREEHAND },
    { "curve_typelinear",       WIDGET_CURVE_TYPELINEAR },
    { "button_fx_entry",        WIDGET_BUTTON_FX_ENTRY },
    { "subrender_entry_toggle", WIDGET_SUBRENDER_ENTRY_TOGGLE },
    { "label_effectname",       WIDGET_LABEL_EFFECTNAME },
    { "label_effectanim_name",  WIDGET_LABEL_EFFECTANIM_NAME },
    { "value_friendlyname",     WIDGET_VALUE_FRIENDLYNAME },
    { "button_entry_toggle",    WIDGET_BUTTON_ENTRY_TOGGLE },
    { "transition_loop",        WIDGET_TRANSITION_LOOP },
    { "transition_enabled",     WIDGET_TRANSITION_ENABLED },
    { "combo_curve_fx_param",   WIDGET_COMBO_CURVE_FX_PARAM },
    { "fx_m1",                  WIDGET_FX_M1 },
    { "fx_m2",                  WIDGET_FX_M2 },
    { "fx_m3",                  WIDGET_FX_M3 },
    { "fx_m4",                  WIDGET_FX_M4 },
    { "fx_mnone",               WIDGET_FX_MNONE },
    {"sample_loop_box",         WIDGET_SAMPLE_LOOP_BOX },
    {"button_084",              WIDGET_BUTTON_084 },
    {"button_083",              WIDGET_BUTTON_083 },
    {"video_navigation_buttons", WIDGET_VIDEO_NAVIGATION_BUTTONS},
    {"button_samplestart",       WIDGET_BUTTON_SAMPLESTART},
    {"button_sampleend",         WIDGET_BUTTON_SAMPLEEND},
    {"speed_slider",             WIDGET_SPEED_SLIDER},
    {"slow_slider",              WIDGET_SLOW_SLIDER},
    {"button_200",               WIDGET_BUTTON_200}, // mask button
    {"frame_fxtree",             WIDGET_FRAME_FXTREE},
    {"fxpanel",                  WIDGET_FXPANEL},
    {"vjframerate",              WIDGET_VJFRAMERATE },
    {"scrolledwindow49",         WIDGET_SCROLLEDWINDOW49 },
    {"samplegrid_frame",         WIDGET_SAMPLEGRID_FRAME },
    {"markerframe",              WIDGET_MARKERFRAME },
    {"fxanimcontrols",           WIDGET_FXANIMCONTROLS },
    {"curve_container",          WIDGET_CURVECONTAINER },
    {"SRTframe",                 WIDGET_SRTFRAME },
    {"frame_fxtree1",            WIDGET_FRAME_FXTREE1 },
    {"button_5_4",               WIDGET_BUTTON_5_4 },
    {"toggle_multicast",         WIDGET_TOGGLE_MULTICAST },
    {"cali_save_button",         WIDGET_CALI_SAVE_BUTTON },
    {"veejay_box",               WIDGET_VEEJAY_BOX }, 
    { "vbox633",                 WIDGET_VBOX633 },
    { "hbox910",                 WIDGET_HBOX910 },
    { "hbox27",                  WIDGET_HBOX27 },
    { "sample_bank_hbox",        WIDGET_SAMPLE_BANK_HBOX },
    { "button_samplebank_prev",  WIDGET_BUTTON_SAMPLEBANK_PREV },
    { "button_samplebank_next",  WIDGET_BUTTON_SAMPLEBANK_NEXT },
    { "spin_samplebank_select",  WIDGET_SPIN_SAMPLEBANK_SELECT },
    { "hbox709",                 WIDGET_HBOX709 },
    { "sample_panel",            WIDGET_SAMPLE_PANEL },
    { "vbox623",                 WIDGET_VBOX623 },
    { "slider_box_p0",           WIDGET_SLIDER_BOX_G0 },
    { "slider_box_p1",           WIDGET_SLIDER_BOX_G1 },
    { "slider_box_p2",           WIDGET_SLIDER_BOX_G2 },
    { "slider_box_p3",           WIDGET_SLIDER_BOX_G3 },
    { "slider_box_p4",           WIDGET_SLIDER_BOX_G4 },
    { "slider_box_p5",           WIDGET_SLIDER_BOX_G5 },
    { "slider_box_p6",           WIDGET_SLIDER_BOX_G6 },
    { "slider_box_p7",           WIDGET_SLIDER_BOX_G7 },
    { "slider_box_p8",           WIDGET_SLIDER_BOX_G8 },
    { "slider_box_p9",           WIDGET_SLIDER_BOX_G9 },
    { "slider_box_p10",          WIDGET_SLIDER_BOX_G10 },
    { "curve_chain_togglechain", WIDGET_CURVE_CHAIN_TOGGLECHAIN },
    { "spin_macrodelay",         WIDGET_SPIN_MACRODELAY },
    { "macrorecord1",            WIDGET_MACRORECORD1 },
    { "macroplay1",              WIDGET_MACROPLAY1 },
    { "macrostop1",              WIDGET_MACROSTOP1 },
    { "spin_sampleduration",   WIDGET_SPIN_SAMPLEDURATION },
    { NULL, -1 },
};

static struct
{
    const char *text;
} tooltips[] =
{
    {"Mouse left: Set in point,\nMouse right: Set out point,\nDouble click: Clear selection,\nMouse middle: Drag selection"},
    {"Mouse left/right: Play slot,\nShift + Mouse left: Put sample in slot.\nYou can also put selected samples."},
    {"Mouse left: Select slot (sample in slot),\nMouse double click: Play sample in slot,\nShift + Mouse left: Set slot as mixing current mixing channel"},
    {"Select a SRT sequence to edit"},
    {"Double click: add effect to current entry in chain list,\n [+] Shift L: add disabled,\n [+] Ctrl L: add to selected sample"},
    {"Filter the effects list by any string"},
    {"Shift + Mouse left : Toogle selected fx,\nControl + Mouse left : Toogle selected fx anim"},
    {NULL},
};

enum
{
    TOOLTIP_TIMELINE = 0,
    TOOLTIP_QUICKSELECT = 1,
    TOOLTIP_SAMPLESLOT = 2,
    TOOLTIP_SRTSELECT = 3,
    TOOLTIP_FXSELECT = 4,
    TOOLTIP_FXFILTER = 5,
    TOOLTIP_FXCHAINTREE = 6
};

#define FX_PARAMETER_DEFAULT_NAME "<none>"
#define FX_PARAMETER_VALUE_DEFAULT_HINT ""

enum
{
    STREAM_NO_STREAM = 0,
    STREAM_RED = 9,
    STREAM_GREEN = 8,
    STREAM_GENERATOR = 7,
    STREAM_CALI = 6,
    STREAM_WHITE = 4,
    STREAM_VIDEO4LINUX = 2,
    STREAM_DV1394 = 17,
    STREAM_NETWORK = 13,
    STREAM_MCAST = 14,
    STREAM_YUV4MPEG = 1,
    STREAM_AVFORMAT = 12,
    STREAM_CLONE = 15,
    STREAM_VLOOP = 3,
    STREAM_PICTURE = 5
};

enum
{
    COLUMN_INT = 0,
    COLUMN_STRING0,
    COLUMN_STRINGA ,
    COLUMN_STRINGB,
    COLUMN_STRINGC,
    N_COLUMNS
};

enum
{
    ENTRY_FXID = 0,
    ENTRY_ISVIDEO = 1,
    ENTRY_NUM_PARAMETERS = 2,
    ENTRY_KF_STATUS = 3,
    ENTRY_KF_TYPE = 4,
    ENTRY_TRANSITION_ENABLED = 5,
    ENTRY_TRANSITION_LOOP = 6,
    ENTRY_SOURCE = 7,
    ENTRY_CHANNEL = 8,
    ENTRY_VIDEO_ENABLED = 9,
    ENTRY_SUBRENDER_ENTRY = 10,
    ENTRY_P0 = 11,
    ENTRY_P1 = 12,
    ENTRY_P2 = 13,
    ENTRY_P3 = 14,
    ENTRY_P4 = 15,
    ENTRY_P5 = 16,
    ENTRY_P6 = 17,
    ENTRY_P8 = 18,
    ENTRY_P9 = 19,
    ENTRY_P10 = 20,
    ENTRY_P11 = 21,
    ENTRY_P12 = 22,
    ENTRY_P13 = 23,
    ENTRY_P14 = 24,
    ENTRY_P15 = 25,
    ENTRY_LAST = 26
    /*
    ENTRY_P1 = 16,
    ENTRY_P2 = 21,
    ENTRY_P3 = 26,
    ENTRY_P4 = 31,
    ENTRY_P5 = 36,
    ENTRY_P6 = 41,
    ENTRY_P8 = 46,
    ENTRY_P9 = 51,
    ENTRY_P10 = 56,
    ENTRY_P11 = 61,
    ENTRY_P12 = 66,
    ENTRY_P13 = 71,
    ENTRY_P14 = 76,
    ENTRY_P15 = 81,
    ENTRY_LAST = 86 */
};

#define ENTRY_PARAMSET ENTRY_P0

enum
{
    SL_ID = 0,
    SL_DESCR = 1,
    SL_TIMECODE = 2
};

enum
{
    HINT_CHAIN = 0,
    HINT_EL = 1,
    HINT_MIXLIST = 2,
    HINT_SAMPLELIST = 3,
    HINT_ENTRY = 4,
    HINT_SAMPLE = 5,
    HINT_SLIST = 6,
    HINT_V4L = 7,
    HINT_RECORDING = 8,
    HINT_RGBSOLID = 9,
    HINT_BUNDLES = 10,
    HINT_HISTORY = 11,
    HINT_MARKER = 12,
    HINT_KF = 13,
    HINT_SEQ_ACT = 14,
    HINT_SEQ_CUR = 15,
    HINT_GENERATOR =16,
	HINT_MACRO=17,
    HINT_KEYS = 18,
    HINT_MACRODELAY = 19,
    NUM_HINTS = 20,
};

enum
{
    PAGE_CONSOLE =0,
    PAGE_FX = 3,
    PAGE_EL = 1,
    PAGE_SAMPLEEDIT = 2,
};

typedef struct
{
    int channel;
    int dev;
} stream_templ_t;

enum
{
    V4L_DEVICE=0,
    DV1394_DEVICE=1,
};

typedef enum
{
    FILE_FILTER_DEFAULT =0,
    FILE_FILTER_SL,
    FILE_FILTER_XML,
    FILE_FILTER_YUV,
    FILE_FILTER_CFG,
    FILE_FILTER_NONE,
} file_filter_t;

// NUM_HINTS_MAX must be larger than NUM_HINTS and divisable by 8
#define NUM_HINTS_MAX 32

typedef struct
{
    int reload_hint[NUM_HINTS_MAX];
    int reload_hint_checksums[NUM_HINTS_MAX];
    int selected_vims_accel[2];
    int entry_tokens[ENTRY_LAST];
    int streams[4096];
    int recording[2];

    int selected_chain_entry;
    int selected_el_entry;
    int selected_vims_entry;

    int render_record;
    int iterator;
    int selected_effect_id;
    gboolean reload_force_avoid;
    int playmode;
    int sample_rec_duration;
    int selected_mix_sample_id;
    int selected_mix_stream_id;
    int selected_rgbkey;
    int priout_lock;
    int pressed_key;
    int pressed_mod;
    int keysnoop;
    int randplayer;
    stream_templ_t  strtmpl[2]; // v4l, dv1394
    int selected_parameter_id; // current kf
    int selected_vims_type;
    char *selected_vims_args;
    int cali_duration;
    int cali_stage;
    int expected_num_samples;
    int expected_num_streams;
    int real_num_samples;
    int real_num_streams;
} veejay_user_ctrl_t;

typedef struct
{
    float fps;
    float   ratio;
    int num_files;
    int *offsets;
    int num_frames;
    int width;
    int height;
} veejay_el_t;

enum
{
    RUN_STATE_LOCAL = 1,
    RUN_STATE_REMOTE = 2,
};

typedef struct
{
    gint event_id;
    gint params;
    gchar *format;
    gchar *descr;
    gchar *args;
} vims_t;

typedef struct
{
    gint keyval;
    gint state;
    gchar *args;
    gchar *vims;
    gint event_id;
} vims_keys_t;

static  int user_preview = 0;
static int NUM_BANKS = 50;
static  int NUM_SAMPLES_PER_PAGE = 12;
static int SAMPLEBANK_COLUMNS = 6;
static int SAMPLEBANK_ROWS = 2;
static int use_key_snoop = 0;

#define G_MOD_OFFSET 200
#define SEQUENCE_LENGTH 1024
#define MEM_SLOT_SIZE 32

static vims_t vj_event_list[VIMS_MAX];
static  vims_keys_t vims_keys_list[VIMS_MAX];
static  int vims_verbosity = 0;
#define   livido_port_t vevo_port_t
static int cali_stream_id = 0;
static int cali_onoff     = 0;
static int geo_pos_[2] = { -1,-1 };
static vevo_port_t *fx_list_ = NULL;
typedef struct
{
    GtkWidget *title;
    GtkWidget *timecode;
    GtkWidget *image;
    GtkWidget *frame;
    GtkWidget *event_box;
    GtkWidget *upper_hbox;
    GtkWidget *hotkey;
} sample_gui_slot_t;

typedef struct
{
    GtkWidget *frame;
    GtkWidget *image;
    GtkWidget *event_box;
    GtkWidget *main_vbox;
    GdkPixbuf *pixbuf_ref;
    gint sample_id;
    gint sample_type;
} sequence_gui_slot_t;

typedef struct
{
    gint slot_number;
    gint sample_id;
    gint sample_type;
    gchar *title;
    gchar *timecode;
    gint refresh_image;
    GdkPixbuf *pixbuf;
    guchar *rawdata;
} sample_slot_t;

typedef struct
{
    gint seq_start;
    gint seq_end;
    gint w;
    gint h;
    sequence_gui_slot_t **gui_slot;
    sample_slot_t *selected;
    gint envelope_size;
} sequence_envelope;

typedef struct
{
    sample_slot_t *sample;
} sequence_slot_t;


typedef struct
{
    gint bank_number;
    gint page_num;
    sample_slot_t **slot;
    sample_gui_slot_t **gui_slot;
} sample_bank_t;

typedef struct
{
    char *hostname;
    int port_num;
    int state;    // IDLE, PLAYING
    struct timeval p_time;
    int w_state; // watchdog state
    int w_delay;
} watchdog_t;

typedef struct {
    char **description;
} value_hint;

#define SAMPLE_MAX_PARAMETERS 32

typedef struct {
    int defaults[SAMPLE_MAX_PARAMETERS];
    int min[SAMPLE_MAX_PARAMETERS];
    int max[SAMPLE_MAX_PARAMETERS];
    char description[150];
    char *param_description[SAMPLE_MAX_PARAMETERS];
    value_hint *hints[SAMPLE_MAX_PARAMETERS];
    int id;
    int is_video;
    int num_arg;
    int has_rgb;
    int is_gen;
} effect_constr;

#define EFFECT_LIST_SIZE 4096
typedef struct
{
    GtkBuilder *main_window;
    vj_client   *client;
    int status_tokens[STATUS_TOKENS];   /* current status tokens */
    int *history_tokens[4];     /* list last known status tokens */
    int status_passed;
    int status_lock;
    int slider_lock;
    int parameter_lock;
    int entry_lock;
    int sample[2];
    int selection[3];
    gint status_pipe;
    int sensitive;
    int launch_sensitive;
    struct timeval  alarm;
    struct timeval  timer;
//  GIOChannel  *channel;
    GdkVisual *color_map;
    gint connecting;
//  gint logging;
    gint streamrecording;
    gint samplerecording;
//  gint cpumeter;
    gint cachemeter;
    gint image_w;
    gint image_h;
    veejay_el_t el;
    veejay_user_ctrl_t uc;
    effect_constr **effect_info;
    GList       *devlist;
    GList       *chalist;
    GList       *editlist;
    GList       *elref;
    long        window_id;
    int run_state;
    int play_direction;
    int load_image_slot;
    GtkWidget   *sample_bank_pad;
    GtkWidget   *quick_select;
    GtkWidget   *sample_sequencer;
    sample_bank_t   **sample_banks;
    sample_slot_t   *selected_slot;
    sample_slot_t   *selection_slot;
    sample_gui_slot_t *selected_gui_slot;
    sample_gui_slot_t *selection_gui_slot;
    sequence_envelope *sequence_view;
    sequence_envelope *sequencer_view;
    int sequencer_col;
    int sequencer_row;
    int sequence_playing;
    gint current_sequence_slot;
//  GtkKnob     *audiovolume_knob;
//  GtkKnob     *speed_knob;
    int image_dimensions[2];
//  guchar  *rawdata;
    int prev_mode;
    GtkWidget   *tl;
    GtkWidget   *curve;
    int status_frame;
    int key_id;
    GdkRGBA    *normal;
    gboolean key_now;
    void *mt;
    watchdog_t  watch;
    int vims_line;
    void *midi;
    struct timeval  time_last;
    uint8_t     *cali_buffer;
} vj_gui_t;

enum
{
    STATE_STOPPED = 0,
    STATE_RECONNECT = 1,
    STATE_PLAYING = 2,
    STATE_CONNECT = 3,
    STATE_DISCONNECT = 4,
    STATE_BUSY = 5,
    STATE_LOADING = 6,
    STATE_WAIT_FOR_USER = 7,
    STATE_QUIT = 8,
};

enum
{
    FXC_ID = 0,
    FXC_FXID = 1,
    FXC_FXSTATUS = 2,
    FXC_KF = 3,
    FXC_MIXING = 4,
    FXC_SUBRENDER = 5,
    FXC_KF_STATUS = 6,
    FXC_N_COLS = 7,
};

enum {
	MACRO_FRAME = 0,
	MACRO_DUP = 1,
	MACRO_LOOP = 2,
	MACRO_MSG_SEQ = 3,
	MACRO_VIMS = 4,
	MACRO_VIMS_DESCR = 5,
};

enum
{
    V4L_NUM=0,
    V4L_NAME=1,
    V4L_SPINBOX=2,
    V4L_LOCATION=3,
};

enum
{
    VIMS_ID=0,
    VIMS_KEY=1,
    VIMS_MOD=2,
    VIMS_DESCR=3,
    VIMS_PARAMS=4,
    VIMS_FORMAT=5,
    VIMS_CONTENTS=6,
};

enum
{
    VIMS_LIST_ITEM_ID=0,
    VIMS_LIST_ITEM_DESCR=1
};

typedef struct
{
    const char *text;
} widget_name_t;

typedef struct
{
    GtkListStore       *list;
    GtkTreeModelSort   *sorted;
    GtkTreeModelFilter *filtered;
} effectlist_models;

typedef struct
{
    effectlist_models  stores[3];
    gchar              *filter_string;
} effectlist_data;

static widget_name_t *slider_box_names_ = NULL;
static widget_name_t *slider_names_ = NULL;
static widget_name_t *param_frame_ = NULL;
static widget_name_t *param_names_ = NULL;
static widget_name_t *param_incs_ = NULL;
static widget_name_t *param_decs_ = NULL;
static widget_name_t *gen_names_ = NULL;
static widget_name_t *gen_incs_ = NULL;
static widget_name_t *gen_decs_ = NULL;
static effectlist_data fxlist_data;

#define MAX_PATH_LEN 1024
#define VEEJAY_MSG_OUTPUT   4
#define GENERATOR_PARAMS 11

#define SEQUENCER_COL 10
#define SEQUENCER_ROW 10
static guint macro_line[4] = { 0,0,0,0 };

static vj_gui_t *info = NULL;
void *get_ui_info() { return (void*) info; }
void reloaded_schedule_restart();
/* global pointer to the sample-bank */

/* global pointer to the effects-source-list */
static GtkWidget *effect_sources_tree = NULL;
static GtkListStore *effect_sources_store = NULL;
static GtkTreeModel *effect_sources_model = NULL;


static GtkWidget    *cali_sourcetree = NULL;
static GtkListStore *cali_sourcestore = NULL;
static GtkTreeModel *cali_sourcemodel = NULL;

/* global pointer to the editlist-tree */
static GtkWidget *editlist_tree = NULL;
static GtkListStore *editlist_store = NULL;
static GtkTreeModel *editlist_model = NULL;
//void    gtk_configure_window_cb( GtkWidget *w, GdkEventConfigure *ev, gpointer data );
static int get_slider_val(const char *name);
static int get_slider_val2(GtkWidget *w);
void vj_msg(int type, const char format[], ...);
//static  void    vj_msg_detail(int type, const char format[], ...);
void msg_vims(char *message);
static void multi_vims(int id, const char format[],...);
static void single_vims(int id);
static gdouble get_numd(const char *name);
static int get_nums(const char *name);
static gchar *get_text(const char *name);
static void put_text(const char *name, char *text);
static void set_toggle_button(const char *name, int status);
static void update_slider_gvalue(const char *name, gdouble value );
static void update_slider_value2(GtkWidget *w, gint value, gint scale);
static void update_slider_range2(GtkWidget *w, gint min, gint max, gint value, gint scaled);

static void update_slider_value(const char *name, gint value, gint scale);
static void update_slider_range(const char *name, gint min, gint max, gint value, gint scaled);
//static  void update_knob_range( GtkWidget *w, gdouble min, gdouble max, gdouble value, gint scaled );
static void update_spin_range2(GtkWidget *w, gint min, gint max, gint val);
static void update_spin_range(const char *name, gint min, gint max, gint val);
static void update_spin_incr(const char *name, gdouble step, gdouble page);
//static void update_knob_value(GtkWidget *w, gdouble value, gdouble scale );
static void update_spin_value(const char *name, gint value);
static void update_label_i2(GtkWidget *label, int num, int prefix);
static void update_label_i(const char *name, int num, int prefix);
static void update_label_f(const char *name, float val);
static void update_label_str(const char *name, gchar *text);
static void update_globalinfo(int *his, int p, int k);
static gint load_parameter_info();
static void load_v4l_info();
static void reload_editlist_contents();
static void load_effectchain_info();
static void set_feedback_status();
static void load_effectlist_info();
static void load_sequence_list();
static void load_generator_info();
static void load_samplelist_info(gboolean with_reset_slotselection);
static void load_editlist_info();
static void set_pm_page_label(int sample_id, int type);
static void notebook_set_page(const char *name, int page);
static void hide_widget(const char *name);
static void show_widget(const char *name);
static void setup_tree_spin_column(const char *tree_name, int type, const char *title);
static void setup_tree_text_column( const char *tree_name, int type, const char *title, int expand );
static void setup_tree_pixmap_column( const char *tree_name, int type, const char *title );
gchar *_utf8str( const char *c_str );
static gchar *recv_vims(int len, int *bytes_written);
static gchar *recv_vims_args(int slen, int *bytes_written, int *arg0, int *arg1);
static GdkPixbuf *update_pixmap_entry( int status );
static gboolean chain_update_row(GtkTreeModel * model, GtkTreePath * path, GtkTreeIter * iter,gpointer data);
int resize_primary_ratio_y();
int resize_primary_ratio_x();
static void update_rgbkey();
static int count_textview_buffer(const char *name);
static void clear_textview_buffer(const char *name);
static void init_recorder(int total_frames, gint mode);
static void reload_bundles();
static void update_rgbkey_from_slider();
static gchar *get_textview_buffer(const char *name);
static void create_slot(gint bank_nr, gint slot_nr, gint w, gint h);
static void setup_samplebank(gint c, gint r, GtkAllocation *allocation, gint *image_w, gint *image_h);
static int add_sample_to_sample_banks( int bank_page,sample_slot_t *slot );
static void update_sample_slot_data(int bank_num, int slot_num, int id, gint sample_type, gchar *title, gchar *timecode);
static gboolean on_slot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data);
static void add_sample_to_effect_sources_list(gint id, gint type, gchar *title, gchar *timecode);
static void set_activation_of_slot_in_samplebank(gboolean activate);
static int bank_exists( int bank_page, int slot_num );
static int find_bank_by_sample(int sample_id, int sample_type, int *slot );
static int add_bank( gint bank_num  );
static void set_selection_of_slot_in_samplebank(gboolean active);
static void remove_sample_from_slot();
static void create_ref_slots(int envelope_size);
static void create_sequencer_slots(int x, int y);
void clear_samplebank_pages();
void free_samplebank(void);
void reset_samplebank(void);
int verify_bank_capacity(int *bank_page_, int *slot_, int sample_id, int sample_type );
static void widget_get_rect_in_screen (GtkWidget *widget, GdkRectangle *r);
static void update_curve_widget( GtkWidget *curve );
/* not used */ /* static void update_curve_accessibility(const char *name); */
static void reset_tree(const char *name);
static void reload_srt();
static void reload_fontlist();
static void indicate_sequence( gboolean active, sequence_gui_slot_t *slot );
static void set_textview_buffer(const char *name, gchar *utf8text);
void interrupt_cb();
int get_and_draw_frame(int type, char *wid_name);
GdkPixbuf *vj_gdk_pixbuf_scale_simple( GdkPixbuf *src, int dw, int dh, GdkInterpType inter_type );
static void vj_kf_select_parameter(int id);
static void vj_kf_refresh(gboolean force);
static void vj_kf_reset();
static void veejay_stop_connecting(vj_gui_t *gui);
void reload_macros();
void reportbug();
void select_chain_entry(int entry);

GtkWidget *glade_xml_get_widget_( GtkBuilder *m, const char *name );

gboolean gveejay_idle(gpointer data)
{
    if(gveejay_running())
    {
        int sync = 0;
        if( is_alive(&sync) == FALSE )
        {
          return FALSE;
        } 
        if( sync )
        {
          if( gveejay_time_to_sync( get_ui_info() ) )
          {
            veejay_update_multitrack( get_ui_info() );
          }
          vj_midi_handle_events( info->midi );
        }
    }
    return TRUE;
}


static void init_widget_cache()
{
    // TODO: add more mappings to reduce the number of times we need to call glade_xml_get_widget_ from update_gui() 
    memset(widget_cache, 0, sizeof(widget_cache));

    for( int i = 0; widget_map[i].name != NULL; i ++ ) {
        widget_cache[ widget_map[i].id ] = glade_xml_get_widget_( info->main_window, widget_map[i].name );
        if( widget_cache[ widget_map[i].id ] == NULL ){
            veejay_msg(VEEJAY_MSG_ERROR, "Widget '%s' does not exist", widget_map[i].name );
        }
    }
}

/* NOT USED
static void identify_widget(GtkWidget *w)
{
    for( int i = 0; widget_map[i].name != NULL; i ++ ) {
        if( widget_cache[ widget_map[i].id ] == w ) {
            veejay_msg(VEEJAY_MSG_DEBUG, "Widget %p is %s", w, widget_map[i].name );
            return;
        }
    }
    veejay_msg(VEEJAY_MSG_DEBUG, "Widget %p is not in widget_cache");
}
*/

static gboolean is_edl_displayed()
{
    int panel_page = gtk_notebook_get_current_page( GTK_NOTEBOOK( widget_cache[ WIDGET_NOTEBOOK18 ] ));
    int sample_page = gtk_notebook_get_current_page( GTK_NOTEBOOK( widget_cache[ WIDGET_NOTEBOOK15] ));

    if( panel_page == 5 && sample_page == 2 )
        return TRUE;
    return FALSE;
}

static gboolean is_fxanim_displayed()
{
    int fxanim_page = gtk_notebook_get_current_page( GTK_NOTEBOOK( widget_cache[ WIDGET_NOTEBOOK18 ] ));
    if( fxanim_page == 1)
        return TRUE;
    return FALSE;
}

gboolean   periodic_pull(gpointer data)
{
    int deckpage = gtk_notebook_get_current_page( GTK_NOTEBOOK( widget_cache[ WIDGET_NOTEBOOK18 ] ));

    int pm = info->status_tokens[PLAY_MODE];
    if( pm == MODE_STREAM && deckpage == 5 && info->status_tokens[STREAM_TYPE] == STREAM_GENERATOR)
    {
        info->uc.reload_hint[HINT_GENERATOR] = 1;
    }

    if( pm != MODE_PLAIN )
    {
        info->uc.reload_hint[HINT_ENTRY] = 1;
        info->uc.reload_hint[HINT_CHAIN] = 1;
    }

    return TRUE;
}

static int data_checksum(char *data,size_t len)
{
    int i;
    int checksum = 0;
    for( i = 0; i < len; i ++ ) {
        checksum = (checksum >> 1) + ((checksum & 1) << 15 );
        checksum += (int) data[i];
        checksum &= 0xffff;
    }
    return checksum;
}

void reset_cali_images( int type, char *wid_name );

gboolean disable_sample_image = FALSE;

void set_disable_sample_image(gboolean status)
{
    disable_sample_image = status;
}

void add_class(GtkWidget *widget, const char *name)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
    gtk_style_context_add_class(ctx, name);
}

void remove_class(GtkWidget *widget, const char *name)
{
    GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
    gtk_style_context_remove_class(ctx, name);
}

static gchar* strduplastn(gchar *title) {
	gchar *reversed = g_strreverse(title);
	gchar *part = g_strndup(reversed,12);
	gchar *reverse = g_strreverse(part);
	gchar *result = g_strdup(reverse);
	g_free(part);
	return result;
}

GtkWidget *glade_xml_get_widget_( GtkBuilder *m, const char *name )
{
    GtkWidget *widget = GTK_WIDGET (gtk_builder_get_object( m , name ));
    if(!widget)
    {
        return NULL;
    }
#ifdef STRICT_CHECKING
    assert( widget != NULL );
#endif
    return widget;
}

static void add_class_by_name(const char *widget_name, const char *name) {
  GtkWidget *widget = glade_xml_get_widget_(info->main_window,widget_name);
  add_class(widget, name);
}

void gtk_notebook_set_current_page__( GtkWidget *w, gint num, const char *f, int line )
{
    gtk_notebook_set_current_page( GTK_NOTEBOOK(w), num );
}

void gtk_widget_set_size_request__( GtkWidget *w, gint iw, gint h, const char *f, int line )
{
    gtk_widget_set_size_request(w, iw, h );
}

#ifndef STRICT_CHECKING
#define gtk_widget_set_size_request_(a,b,c) gtk_widget_set_size_request(a,b,c)
#define gtk_notebook_set_current_page_(a,b) gtk_notebook_set_current_page(a,b)
#else
#define gtk_widget_set_size_request_(a,b,c) gtk_widget_set_size_request__(a,b,c,__FUNCTION__,__LINE__)
#define gtk_notebook_set_current_page_(a,b) gtk_notebook_set_current_page__(a,b,__FUNCTION__,__LINE__)
#endif

static struct
{
    gchar *text;
} text_msg_[] =
{
    {   "Running realtime" },
    {   NULL },
};

enum {
    TEXT_REALTIME = 0
};

static struct
{
    const char *name;
} capt_label_set[] =
{
    { "label333" }, //brightness
    { "label334" }, //contrast
    { "label336" }, //hue
    { "label777" }, //saturation
    { "label29" }, //temperature
    { "label337"}, //gamma
    { "label25" }, //sharpness
    { "label20" }, //gain
    { "label21" }, //red balance
    { "label22" }, //blue balance
    { "label23" }, //green balance
    { "label20" }, //gain
    { "label26" }, //bl_compensate
    { "label34" }, //whiteness
    { "label33" }, //blacklevel
    { "label31" }, //exposure
    { "label24" }, //autowhitebalance
    { "label27" }, //autogain
    { "label28" }, //autohue
    { "label30" }, //fliph
    { "label32" }, //flipv
    { NULL },
};

static struct
{
    const char *name;
} capt_card_set[] =
{
    { "v4l_brightness" },
    { "v4l_contrast" },
    { "v4l_hue"  },
    { "v4l_saturation" },
    { "v4l_temperature"},
    { "v4l_gamma" },
    { "v4l_sharpness" },
    { "v4l_gain" },
    { "v4l_redbalance" },
    { "v4l_bluebalance" },
    { "v4l_greenbalance" },
    { "v4l_gain" },
    { "v4l_backlightcompensation"},
    { "v4l_whiteness"},
    { "v4l_black_level" },
    { "v4l_exposure" },
    { "check_autowhitebalance"},
    { "check_autogain" },
    { "check_autohue" },
    { "check_flip" },
    { "check_flipv"},
    { NULL },
};
#define CAPT_CARD_SLIDERS 16
#define CAPT_CARD_BOOLS    5

static int preview_box_w_ = MAX_PREVIEW_WIDTH;
static int preview_box_h_ = MAX_PREVIEW_HEIGHT;

static void *bankport_ = NULL;

int vj_get_preview_box_w()
{
    return preview_box_w_;
}

int vj_get_preview_box_h()
{
    return preview_box_h_;
}

#ifdef STRICT_CHECKING
static void gtk_image_set_from_pixbuf__( GtkImage *w, GdkPixbuf *p, const char *f, int l )
{
    assert( GTK_IS_IMAGE(w) );
    gtk_image_set_from_pixbuf(w, p);
}

static void gtk_widget_set_sensitive___( GtkWidget *w, gboolean state, const char *f, int l )
{
    assert( GTK_IS_WIDGET(w) );
    gtk_widget_set_sensitive(w, state );
}
#endif

static void select_slot(int pm);

#ifdef STRICT_CHECKING
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive___( w,p,__FUNCTION__,__LINE__ )
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf__( w,p, __FUNCTION__,__LINE__ )
#else
#define gtk_widget_set_sensitive_( w,p ) gtk_widget_set_sensitive( w,p )
#define gtk_image_set_from_pixbuf_(w,p) gtk_image_set_from_pixbuf( w,p )
#endif

enum
{
    TC_SAMPLE_L = 0,
    TC_SAMPLE_F = 1,
    TC_SAMPLE_S = 2,
    TC_SAMPLE_M = 3,
    TC_SAMPLE_H = 4,
    TC_STREAM_F = 5,
    TC_STREAM_M = 6,
    TC_STREAM_H = 7
};

static sample_slot_t *find_slot_by_sample( int sample_id , int sample_type );
static sample_gui_slot_t *find_gui_slot_by_sample( int sample_id , int sample_type );

gchar *_utf8str(const char *c_str)
{
    gsize   bytes_read = 0;
    gsize   bytes_written = 0;
    GError  *error = NULL;
    if(!c_str)
        return NULL;
    char *result = (char*) g_locale_to_utf8( c_str, -1, &bytes_read, &bytes_written, &error );

    if(error)
    {
        g_free(error);
        if( result )
            g_free(result);
        result = NULL;
    }

    return result;
}

/*! \brief Return the widget current state foreground color.
 *
 *  \param w A pointer of calling widget

 *  \return A pointer to GdkRGBA
 */
GdkRGBA *widget_get_fg(GtkWidget *w )
{
    if(!w)
        return NULL;
    GtkStyleContext *sc = gtk_widget_get_style_context(w);
    GdkRGBA *color = (GdkRGBA*)vj_calloc(sizeof(GdkRGBA));
    gtk_style_context_get_color ( sc, gtk_style_context_get_state (sc),
                                  color );
    return color;
}

static void scan_devices( const char *name)
{
    GtkWidget *tree = glade_xml_get_widget_(info->main_window,name);
    GtkListStore *store;
    GtkTreeIter iter;

    reset_tree(name);
    gint len = 0;
    single_vims( VIMS_DEVICE_LIST );
    gchar *text = recv_vims(6,&len);
    if(len <= 0|| !text )
    {
        veejay_msg(VEEJAY_MSG_WARNING, "No capture devices found on veejay server");
        return;
    }
    GtkTreeModel *model = gtk_tree_view_get_model
        (GTK_TREE_VIEW(tree));

    store = GTK_LIST_STORE(model);

    gint offset =0;
    gint i = 0;
    gchar *ptr = text + offset;
    while( offset < len )
    {
        char tmp[4];

        gchar *name = NULL;
        gdouble gchannel = 1.0;
        gchar *loca = NULL;

        gint name_len=0;
        gint loc_len=0;

        strncpy(tmp,ptr+offset,3);
        tmp[3] = '\0';
        offset += 3;
        name_len = atoi( tmp );
        if(name_len <=  0 )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Reading name of capture device: '%s'",ptr+offset );
            return;
        }
        name = strndup( ptr + offset, name_len );
        offset += name_len;
        strncpy( tmp, ptr + offset, 3 );
        tmp[3] = '\0';
        offset += 3;

        loc_len = atoi( tmp );
        if( loc_len <= 0 )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Reading location of capture device");
            return;
        }
        loca = strndup( ptr + offset, loc_len );
        offset += loc_len;
        gchar *thename = _utf8str( name );
        gchar *theloca = _utf8str( loca );

        gtk_list_store_append( store, &iter);
        gtk_list_store_set(store, &iter,
                           V4L_NUM, i,
                           V4L_NAME, thename,
                           V4L_SPINBOX, gchannel,
                           V4L_LOCATION, theloca,
                           -1);

        g_free(thename);
        g_free(theloca);

        free(loca);
        free(name);
        i ++;
    }
    free(text);

    gtk_tree_view_set_model(GTK_TREE_VIEW(tree), model );
}

static void scan_generators( const char *name)
{
    GtkWidget *tree = glade_xml_get_widget_(info->main_window,name);
    GtkListStore *store;
    GtkTreeIter iter;

    reset_tree(name);
    gint len = 0;
    single_vims( VIMS_GET_GENERATORS );
    gchar *text = recv_vims(5,&len);
    if(len <= 0|| !text )
    {
        veejay_msg(VEEJAY_MSG_WARNING, "No generators found on veejay server");
        return;
    }
    GtkTreeModel *model = gtk_tree_view_get_model
        (GTK_TREE_VIEW(tree));

    store = GTK_LIST_STORE(model);

    gint offset =0;
    gint i = 0;
    gchar *ptr = text + offset;
    while( offset < len )
    {
        char tmp[4];

        gchar *name = NULL;
        gint name_len=0;

        strncpy(tmp,ptr+offset,3);
        tmp[3] = '\0';
        offset += 3;
        name_len = atoi( tmp );
        if(name_len <=  0 )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "Reading name of generator: '%s'",ptr+offset );
            return;
        }
        name = strndup( ptr + offset, name_len );
        offset += name_len;
        gchar *thename = _utf8str( name );

        gtk_list_store_append( store, &iter);
        gtk_list_store_set(store, &iter,0,thename,-1);

        g_free(thename);

        free(name);
        i ++;
    }
    free(text);

    gtk_tree_view_set_model(GTK_TREE_VIEW(tree), model );
}

static void set_tooltip_by_widget(GtkWidget *w, const char *text)
{
    gtk_widget_set_tooltip_text( w,text );
}

static void set_tooltip(const char *name, const char *text)
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
    if(!w) {
#ifdef STRICT_CHECKING
        veejay_msg(VEEJAY_MSG_ERROR, "Widget '%s' not found",name);
#endif
        return;
    }
    gtk_widget_set_tooltip_text(    w,text );
}

void on_devicelist_row_activated(GtkTreeView *treeview,
                                 GtkTreePath *path,
                                 GtkTreeViewColumn *col,
                                 gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_tree_view_get_model(treeview);
    if(gtk_tree_model_get_iter(model,&iter,path))
    {
        gint channel =  info->uc.strtmpl[0].channel;
        gint num = info->uc.strtmpl[0].dev;

        multi_vims( VIMS_STREAM_NEW_V4L,"%d %d",
                num,
                channel
                );
    }
}

gboolean device_selection_func( GtkTreeSelection *sel,
                               GtkTreeModel *model,
                               GtkTreePath  *path,
                               gboolean path_currently_selected,
                               gpointer userdata )
{
    GtkTreeIter iter;
    GValue val = { 0, };
    if( gtk_tree_model_get_iter( model, &iter, path ) )
    {
        gint num = 0;
        //gtk_tree_model_get(model, &iter, V4L_NUM,&num, -1 );
        gchar *file = NULL;
        gtk_tree_model_get( model, &iter, V4L_LOCATION, &file, -1 );
        sscanf( file, "/dev/video%d", &num );
        if(! path_currently_selected )
        {
            gtk_tree_model_get_value(model, &iter, V4L_SPINBOX, &val);
            info->uc.strtmpl[0].dev = num;
            info->uc.strtmpl[0].channel = (int) g_value_get_float(&val);
        }
        g_free(file);
    }
    return TRUE;
}

static void setup_v4l_devices()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_v4ldevices");
    GtkListStore *store = gtk_list_store_new( 4, G_TYPE_INT, G_TYPE_STRING, G_TYPE_FLOAT,
                    G_TYPE_STRING );

    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    GtkTreeSelection *sel = gtk_tree_view_get_selection( GTK_TREE_VIEW( tree ) );
    gtk_tree_selection_set_mode( sel, GTK_SELECTION_SINGLE );
    gtk_tree_selection_set_select_function( sel, device_selection_func, NULL,NULL );

//  gtk_tree_view_set_fixed_height_mode( GTK_TREE_VIEW(tree), TRUE );

    g_object_unref( G_OBJECT( store ));
    setup_tree_text_column( "tree_v4ldevices", V4L_NUM, "#",0 );
    setup_tree_text_column( "tree_v4ldevices", V4L_NAME, "Device Name",0);
    setup_tree_spin_column( "tree_v4ldevices", V4L_SPINBOX, "Channel");
    setup_tree_text_column( "tree_v4ldevices", V4L_LOCATION, "Location",0);

    g_signal_connect( tree, "row-activated",
        (GCallback) on_devicelist_row_activated, NULL );

    //scan_devices( "tree_v4ldevices" );
}

gboolean vims_macro_selection_func( GtkTreeSelection *sel, GtkTreeModel *model, GtkTreePath *path, gboolean path_currently_selected, gpointer data)
{
	GtkTreeIter iter;
	if( gtk_tree_model_get_iter( model, &iter, path) ) {
			
		guint frame_num = 0, dup_num = 0, loop_num = 0, seq_no = 0;

		gchar *message = NULL;
		gtk_tree_model_get( model, &iter, MACRO_FRAME, &frame_num, -1);
		gtk_tree_model_get( model, &iter, MACRO_DUP, &dup_num, -1);
	    gtk_tree_model_get( model, &iter, MACRO_MSG_SEQ, &seq_no, -1);
		gtk_tree_model_get( model, &iter, MACRO_LOOP, &loop_num, -1);
	    gtk_tree_model_get( model, &iter, MACRO_VIMS, &message, -1 );

		macro_line[0] = frame_num;
		macro_line[1] = dup_num;
		macro_line[2] = loop_num;
		macro_line[3] = seq_no;

		update_spin_value( "macro_frame_position", frame_num );
		update_spin_value( "macro_dup_position", dup_num );
		update_spin_value( "macro_loop_position", loop_num );
		put_text( "macro_vims_message", message );
	
		g_free(message);
	}
	return TRUE;
}

static void setup_macros()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "macro_macros" );
	GtkListStore *store = gtk_list_store_new( 6, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING );

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store) );
	GtkTreeSelection *sel = gtk_tree_view_get_selection( GTK_TREE_VIEW(tree) );
	gtk_tree_selection_set_mode( sel, GTK_SELECTION_SINGLE );
	gtk_tree_selection_set_select_function( sel, vims_macro_selection_func, NULL,NULL );

	g_object_unref( G_OBJECT(store) );

	setup_tree_text_column( "macro_macros", MACRO_FRAME, "Frame",0 );
	setup_tree_text_column( "macro_macros", MACRO_DUP, "Dup",0);
	setup_tree_text_column( "macro_macros", MACRO_LOOP, "Loop",0);
	setup_tree_text_column( "macro_macros", MACRO_MSG_SEQ, "#",0);
	setup_tree_text_column( "macro_macros", MACRO_VIMS, "VIMS Message",0);
	setup_tree_text_column( "macro_macros", MACRO_VIMS_DESCR, "Description",0);
}

typedef struct
{
    int id;
    int nl;
    long n1;
    long n2;
    int tf;
} el_ref;

typedef struct
{
    int pos;
    char *filename;
    char *fourcc;
    int num_frames;
} el_constr;


int _effect_is_gen(int effect_id)
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL)
        return ec->is_gen;
    return 0;
}

int _effect_get_mix(int effect_id)
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL)
        return ec->is_video;
    return 0;
}

int _effect_get_rgb(int effect_id)
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL)
        return ec->has_rgb;
    return 0;
}

/*
 * Return the number of parameters from an effect identifier
 *
 * FIXME return too much parameter for some generator
 */
int _effect_get_np(int effect_id)
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL)
        return ec->num_arg;
    return 0;
}

int _effect_get_minmax( int effect_id, int *min, int *max, int index )
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL && index < ec->num_arg) {
        *min = ec->min[index];
        *max = ec->max[index];
        return 1;
    }
    return 0;
}

char *_effect_get_param_description(int effect_id, int param)
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL)
        return ec->param_description[param];
    return FX_PARAMETER_DEFAULT_NAME;
}

char *_effect_get_description(int effect_id)
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL)
        return ec->description;
    return FX_PARAMETER_DEFAULT_NAME;
}

char *_effect_get_hint(int effect_id, int p, int v)
{
    effect_constr *ec = info->effect_info[effect_id];
    if(ec != NULL)
    {
        if( ec->hints[p] == NULL)
                return FX_PARAMETER_VALUE_DEFAULT_HINT;

        return ec->hints[p]->description[v];
    }
    return FX_PARAMETER_VALUE_DEFAULT_HINT;
}

el_constr *_el_entry_new( int pos, char *file, int nf , char *fourcc)
{
    el_constr *el = g_new( el_constr , 1 );
    el->filename = strdup( file );
    el->num_frames = nf;
    el->pos = pos;
    el->fourcc = strdup(fourcc);
    return el;
}

void _el_entry_free( el_constr *entry )
{
    if(entry)
    {
        if(entry->filename) free(entry->filename);
        if(entry->fourcc) free(entry->fourcc);
        free(entry);
    }
}

void _el_entry_reset( )
{
    if(info->editlist != NULL)
    {
        int n = g_list_length( info->editlist );
        int i;
        for( i = 0; i <= n ; i ++)
            _el_entry_free( g_list_nth_data( info->editlist, i ) );
        g_list_free(info->editlist);
        info->editlist=NULL;
    }
}

int _el_get_nframes( int pos )
{
    int n = g_list_length( info->editlist );
    int i;
    for( i = 0; i <= n ; i ++)
    {
        el_constr *el = g_list_nth_data( info->editlist, i );
        if(!el) return 0;
        if(el->pos == pos)
            return el->num_frames;
    }
    return 0;
}

el_ref *_el_ref_new( int row_num,int nl, long n1, long n2, int tf)
{
    el_ref *el = vj_malloc(sizeof(el_ref));
    el->id = row_num;
    el->nl = nl;
    el->n1 = n1;
    el->n2 = n2;
    el->tf = tf;
    return el;
}

void _el_ref_free( el_ref *entry )
{
    if(entry) free(entry);
}

void _el_ref_reset()
{
    if(info->elref != NULL)
    {
        int n = g_list_length( info->elref );
        int i;
        for(i = 0; i < n; i ++ )
        {
            el_ref *edl = g_list_nth_data(info->elref, i );
            if(edl)
                free(edl);
        }
        g_list_free(info->elref);
        info->elref = NULL;
    }
}

int _el_ref_end_frame( int row_num )
{
    int n = g_list_length( info->elref );
    int i;
    for ( i = 0 ; i <= n; i ++ )
    {
        el_ref *el  = g_list_nth_data( info->elref, i );
        if(el->id == row_num )
        {
//          int offset = info->el.offsets[ el->nl ];
//          return (offset + el->n1 + el->n2 );
            return (el->tf + el->n2 - el->n1);
        }
    }
    return 0;
}

int _el_ref_start_frame( int row_num )
{
    int n = g_list_length( info->elref );
    int i;
    for ( i = 0 ; i <= n; i ++ )
    {
        el_ref *el  = g_list_nth_data( info->elref, i );
        if(el->id == row_num )
        {
//          int offset = info->el.offsets[ el->nl ];
//          return (offset + el->n1 );
//          printf("Start pos of row %d : %d = n1, %d = n2, %d = tf\n",
//              row_num,el->n1,el->n2, el->tf );
            return (el->tf);
        }
    }
    return 0;
}

char *_el_get_fourcc( int pos )
{
    int n = g_list_length( info->editlist );
    int i;
    for( i = 0; i <= n; i ++ )
    {
        el_constr *el = g_list_nth_data( info->editlist, i );
        if(el->pos == pos)
            return el->fourcc;
    }
    return NULL;
}


char *_el_get_filename( int pos )
{
    int n = g_list_length( info->editlist );
    int i;
    for( i = 0; i <= n; i ++ )
    {
        el_constr *el = g_list_nth_data( info->editlist, i );
        if(el->pos == pos)
            return el->filename;
    }
    return NULL;
}

effect_constr* _effect_new( char *effect_line )
{
    effect_constr *ec;
    int descr_len = 0;
    int p,q;
    char len[4];
    //char line[100];
    int offset = 0;

    veejay_memset(len,0,sizeof(len));

    if(!effect_line) return NULL;

    strncpy(len, effect_line, 3);
    sscanf(len, "%03d", &descr_len);
    if(descr_len <= 0) return NULL;

    ec = vj_calloc( sizeof(effect_constr));
    strncpy( ec->description, effect_line+3, descr_len );

    sscanf(effect_line+(descr_len+3), "%03d%1d%1d%1d%02d", &(ec->id),&(ec->is_video),&(ec->has_rgb),&(ec->is_gen), &(ec->num_arg));
    offset = descr_len + 11;
    for(p=0; p < ec->num_arg; p++)
    {
        int len = 0;
        int n = sscanf(effect_line+offset,"%06d%06d%06d%03d",
            &(ec->min[p]), &(ec->max[p]),&(ec->defaults[p]),&len );
        if( n <= 0 )
        {
            veejay_msg(VEEJAY_MSG_ERROR,"Parse error in FX list" );
            break;
        }
        ec->param_description[p] = (char*) vj_calloc(sizeof(char) * (len+1) );
        strncpy( ec->param_description[p], effect_line + offset + 6 + 6 + 6 + 3, len );
        offset += 3;
        offset += len;
        offset+=18;
    }

    for(p=0; p < ec->num_arg; p++)
    {
        int hint_len = 0;
        int n = sscanf( effect_line + offset, "%03d", &hint_len );
        if( n <= 0 )
        {
            veejay_msg(VEEJAY_MSG_ERROR,"Parse error in FX list hints");
            break;
        }

        offset += 3;

        if(hint_len == 0)
            continue;

        ec->hints[p] = (value_hint*) vj_calloc(sizeof(value_hint));
        ec->hints[p]->description = (char**) vj_calloc(sizeof(char*) * (ec->max[p]+2) );
        for(q = 0; q <= ec->max[p]; q ++ )
        {
            int value_hint = 0;
            n = sscanf( effect_line + offset, "%03d", &value_hint );
            if( n != 1) {
                veejay_msg(VEEJAY_MSG_ERROR,"Parse error in FX list value hint");
                break;
            }

            offset += 3;
            ec->hints[p]->description[q] = (char*) vj_calloc(sizeof(char) * value_hint + 1 );
            strncpy( ec->hints[p]->description[q], effect_line + offset, value_hint );
            offset += value_hint;
        }
    }

    return ec;
}

void _effect_free( effect_constr *effect )
{
    if(effect)
    {
        int p;
        for( p = 0; p < effect->num_arg; p ++ ) {
            free( effect->param_description[p] );
        }
        if( effect->hints ) {
            for( p = 0; p < effect->num_arg; p ++ ) {
                if( effect->hints[p] == NULL )
                    continue;
                int q;
                for( q = 0; effect->hints[p]->description[q] != NULL; q ++ ) {
                    free( effect->hints[p]->description[q] );
                }
                free( effect->hints[p]->description );
                free( effect->hints[p] );
            }
        }

        free(effect);
    }
}

void _effect_reset(void)
{
    int i;
    for( i = 0; i < EFFECT_LIST_SIZE; i ++ ) {
        _effect_free( info->effect_info[i] );
        info->effect_info[i] = NULL;
    }
}

static  gchar *get_relative_path(char *path)
{
    return _utf8str( basename( path ));
}

gchar *dialog_save_file(const char *title, const char *current_name)
{
    GtkWidget *parent_window = glade_xml_get_widget_(info->main_window,
                                                     "gveejay_window" );
    GtkWidget *dialog = gtk_file_chooser_dialog_new(title,
                                                    GTK_WINDOW(parent_window),
                                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                                    NULL);

    add_class(dialog, "reloaded" );

    gtk_file_chooser_set_current_name( GTK_FILE_CHOOSER(dialog), current_name );

    if( gtk_dialog_run( GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        gchar *file = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(dialog) );
        gtk_widget_destroy(dialog);
        return file;
    }
    gtk_widget_destroy(dialog);
    return NULL;
}

static void clear_progress_bar( const char *name, gdouble val )
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), val );
}

static struct
{
    const char *descr;
    const char *filter;
} content_file_filters[] = {
    { "AVI Files (*.avi)", "*.avi", },
    { "Digital Video Files (*.dv)", "*.dv" },
    { "Edit Decision List Files (*.edl)", "*.edl" },
    { "PNG (Portable Network Graphics) (*.png)", "*.png" },
    { "JPG (Joint Photographic Experts Group) (*.jpg)", "*.jpg" },
    { NULL, NULL },
};

static void add_file_filters(GtkWidget *dialog, file_filter_t type )
{
    GtkFileFilter *filter = NULL;

    switch(type) {
        case FILE_FILTER_DEFAULT:
            for(int i = 0; content_file_filters[i].descr != NULL ; i ++ )
            {
                filter = gtk_file_filter_new();
                gtk_file_filter_set_name( filter, content_file_filters[i].descr);
                gtk_file_filter_add_pattern( filter, content_file_filters[i].filter);
                gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter );
            }
            break;
        case FILE_FILTER_SL:
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name( filter, "Sample List Files (*.sl)");
            gtk_file_filter_add_pattern( filter, "*.sl");
            gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);
            break;
        case FILE_FILTER_XML:
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name( filter, "Action Files (*.xml)");
            gtk_file_filter_add_pattern( filter, "*.xml");
            gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);
            break;
        case FILE_FILTER_YUV:
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name( filter, "YUV4MPEG files (*.yuv)");
            gtk_file_filter_add_pattern( filter, "*.yuv" );
            gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);
            break;
        case FILE_FILTER_CFG:
            filter = gtk_file_filter_new();
            gtk_file_filter_set_name( filter, "MIDI config files (*.cfg)");
            gtk_file_filter_add_pattern( filter, "*.cfg" );
            gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);
            break;
        default:
            break;
    }

    filter = gtk_file_filter_new();
    gtk_file_filter_set_name( filter, "All Files (*.*)");
    gtk_file_filter_add_pattern( filter, "*");
    gtk_file_chooser_add_filter( GTK_FILE_CHOOSER(dialog), filter);
}


gchar *dialog_open_file(const char *title, file_filter_t type)
{
    static gchar *_file_path = NULL;

    GtkWidget *parent_window = glade_xml_get_widget_(
            info->main_window, "gveejay_window" );
    GtkWidget *dialog = gtk_file_chooser_dialog_new(title,
                                                    GTK_WINDOW(parent_window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                                    NULL);
    add_class( dialog, "reloaded" );

    add_file_filters(dialog, type );
    gchar *file = NULL;
    if( _file_path )
    {
        gtk_file_chooser_set_current_folder_uri(GTK_FILE_CHOOSER(dialog), _file_path);
        g_free(_file_path);
        _file_path = NULL;
    }

    if( gtk_dialog_run( GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
    {
        file = gtk_file_chooser_get_filename( GTK_FILE_CHOOSER(dialog) );
        _file_path = gtk_file_chooser_get_current_folder_uri(GTK_FILE_CHOOSER(dialog));
    }
    gtk_widget_destroy(GTK_WIDGET(dialog));
    return file;
}

static char *produce_os_str()
{
    char os_str[512];
    char cpu_type[32];
    char *simd = vj_calloc( 128 );
#ifdef ARCH_X86
    sprintf(cpu_type,"x86");
#endif
#ifdef ARCH_X86_64
    sprintf(cpu_type, "x86-64");
#endif
#ifdef ARCH_PPC
    sprintf(cpu_type, "ppc");
#endif
#ifdef ARCH_MIPS
    sprintf(cpu_type, "mips");
#endif
#ifdef HAVE_ASM_MMX
    strcat( simd, "MMX ");
#endif
#ifdef HAVE_ASM_MMX2
    strcat( simd, "MMX2 ");
#endif
#ifdef HAVE_ASM_SSE
    strcat( simd, "SSE " );
#endif
#ifdef HAVE_ASM_SSE2
    strcat( simd, "SSE2" );
#endif
#ifdef HAVE_ASM_CMOV
    strcat( simd, "cmov" );
#endif
#ifdef HAVE_ASM_3DNOW
    strcat( simd, "3DNow");
#endif
#ifdef ARCH_PPC
#ifdef HAVE_ALTIVEC
    strcat( simd, "altivec");
#else
    strcat( simd, "no optimizations");
#endif
#endif
#ifdef ARCH_MIPS
    strcat( simd, "no optimizations");
#endif
    sprintf(os_str,"Arch: %s with %s",
        cpu_type, simd );

    return strdup( os_str );
}

void about_dialog()
{
    const gchar *artists[] =
    {
        "Matthijs v. Henten (glade, pixmaps) <matthijs.vanhenten@gmail.com>",
        "Dursun Koca (V-logo)",
        NULL
    };

    const gchar *authors[] =
    {
        "Developed by:",
        "Matthijs v. Henten <matthijs.vanhenten@gmail.com>",
        "Dursun Koca",
        "Niels Elburg <nwelburg@gmail.com>",
        "\n",
        "Contributions by:",
        "Thomas Reinhold <stan@jf-chemnitz.de>",
        "Toni <oc2pus@arcor.de>",
        "d/j/a/y <d.j.a.y@free.fr> (GTK3 port)",
        NULL
    };

    const gchar *web =
    {
        "http://www.veejayhq.net"
    };

    char blob[1024];
    char *os_str = produce_os_str();
    const gchar *donate =
    {
        "You can donate cryptocoins!\n"\
        "Bitcoin: 1PUNRsv8vDt1upTx9tTpY5sH8mHW1DTrKJ\n"
        "or via PayPal: veejayhq@gmail.com\n"
    };

    sprintf(blob, "Veejay - A visual instrument and realtime video sampler for GNU/Linux\n%s\n%s", os_str, donate );

    free(os_str);

    const gchar *license =
    {
    "This program is Free Software; You can redistribute it and/or modify\n" \
    "under the terms of the GNU General Public License as published by\n" \
    "the Free Software Foundation; either version 2, or (at your option)\n"\
    "any later version.\n\n"\
    "This program is distributed in the hope it will be useful,\n"\
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"\
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n"\
    "See the GNU General Public License for more details.\n\n"\
    "For more information , see also: http://www.gnu.org\n"
    };

    char path[MAX_PATH_LEN];
    veejay_memset( path,0, sizeof(path));
    get_gd( path, NULL,  "veejay-logo.png" );
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file
        ( path, NULL );
    GtkWidget *about = g_object_new(
        GTK_TYPE_ABOUT_DIALOG,
        "program_name", "reloaded",
        "name", VEEJAY_CODENAME,
        "version", VERSION,
        "copyright", "(C) 2004 - 2019 N. Elburg et all.",
        "comments", "The graphical interface for Veejay",
        "website", web,
        "authors", authors,
        "artists", artists,
        "comments", blob,
        "license", license,
        "logo", pixbuf, NULL );

    add_class(about,"reloaded");

    g_object_unref(pixbuf);

    g_signal_connect( about , "response", G_CALLBACK( gtk_widget_destroy),NULL);

    GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    gtk_window_set_transient_for(GTK_WINDOW(about),GTK_WINDOW (mainw));
    gtk_window_set_keep_above(GTK_WINDOW(about), TRUE);

    gtk_window_present(GTK_WINDOW(about));
}

gboolean dialogkey_snooper( GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
    GtkWidget *entry = (GtkWidget*) user_data;

    if( !gtk_widget_is_focus( entry ) )
    {
        return FALSE;
    }
#ifdef HAVE_SDL
    if(event->type == GDK_KEY_PRESS)
    {
        gchar tmp[100];
        info->uc.pressed_key = gdk2sdl_key( event->keyval );
        info->uc.pressed_mod = gdk2sdl_mod( event->state );
        gchar *text = gdkkey_by_id( event->keyval );
        gchar *mod  = gdkmod_by_id( event->state );

        if( text )
        {
            if(!mod || strncmp(mod, " ", 1 ) == 0 )
                snprintf(tmp, sizeof(tmp),"%s", text );
            else
                snprintf(tmp, sizeof(tmp), "%s + %s", mod,text);

            gchar *utf8_text = _utf8str( tmp );
            gtk_entry_set_text( GTK_ENTRY(entry), utf8_text);
            g_free(utf8_text);
        }
    }
#endif
    return FALSE;
}

#ifdef HAVE_SDL
static gboolean key_handler( GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
    if(event->type != GDK_KEY_PRESS)
        return FALSE;

    int gdk_keyval = gdk2sdl_key( event->keyval );
    int gdk_state  = gdk2sdl_mod( event->state );
    if( gdk_keyval >= 0 && gdk_state >= 0 )
    {
        char *message = vims_keys_list[(gdk_state * G_MOD_OFFSET)+gdk_keyval].vims;
        if(message)
            msg_vims(message);
    }
    return FALSE;
}
#endif

static int check_format_string( char *args, char *format )
{
    if(!format || !args )
        return 0;
    char dirty[128];
    int n = sscanf( args, format, &dirty,&dirty, &dirty,&dirty, &dirty,&dirty, &dirty,&dirty, &dirty,&dirty );
    return n;
}

int prompt_keydialog(const char *title, char *msg)
{
    if(!info->uc.selected_vims_entry )
        return 0;
    info->uc.pressed_mod = 0;
    info->uc.pressed_key = 0;

    char pixmap[1024];
    veejay_memset(pixmap,0,sizeof(pixmap));
    get_gd( pixmap, NULL, "icon_keybind.png");

    GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    GTK_WINDOW( mainw ),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_CANCEL,
                                                    GTK_RESPONSE_REJECT,
                                                    GTK_STOCK_OK,
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);

    add_class(dialog, "reloaded" );

    GtkWidget *keyentry = gtk_entry_new();
    gtk_entry_set_text( GTK_ENTRY(keyentry), "<press any key>");
    gtk_editable_set_editable( GTK_EDITABLE(keyentry), FALSE );
    gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
    gtk_window_set_resizable( GTK_WINDOW(dialog), FALSE );

    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK( gtk_widget_hide ), G_OBJECT(dialog ) );

    GtkWidget *hbox1 = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 12 );
    gtk_container_set_border_width( GTK_CONTAINER( hbox1 ), 6 );
    GtkWidget *hbox2 = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 12 );
    gtk_container_set_border_width( GTK_CONTAINER( hbox2 ), 6 );

    GtkWidget *icon = gtk_image_new_from_file( pixmap );

    GtkWidget *label = gtk_label_new( msg );
    gtk_container_add( GTK_CONTAINER( hbox1 ), icon );
    gtk_container_add( GTK_CONTAINER( hbox1 ), label );
    gtk_container_add( GTK_CONTAINER( hbox1 ), keyentry );

    GtkWidget *pentry = NULL;

    if(vj_event_list[ info->uc.selected_vims_entry ].params)
    {
        //@ put in default args
        char *arg_str = vj_event_list[ info->uc.selected_vims_entry ].args;
        pentry = gtk_entry_new();
        GtkWidget *arglabel = gtk_label_new("Arguments:");

        if(arg_str)
            gtk_entry_set_text( GTK_ENTRY(pentry), arg_str );
        gtk_editable_set_editable( GTK_EDITABLE(pentry), TRUE );
        gtk_container_add( GTK_CONTAINER(hbox1), arglabel );
        gtk_container_add( GTK_CONTAINER(hbox1), pentry );
    }
#ifdef HAVE_SDL
    if( info->uc.selected_vims_entry )
    {
        char tmp[100];
        char *str_mod = sdlmod_by_id( info->uc.pressed_mod );
        char *str_key = sdlkey_by_id( info->uc.pressed_key );
        int key_combo_ok = 0;

        if(str_mod && str_key )
        {
            snprintf(tmp,100,"VIMS %d : %s + %s",
                info->uc.selected_vims_entry, str_mod, str_key );
            key_combo_ok = 1;
        }else if ( str_key )
        {
            snprintf(tmp, 100,"VIMS %d: %s", info->uc.selected_vims_entry,str_key);
            key_combo_ok = 1;
        }

        if( key_combo_ok )
        {
            gtk_entry_set_text( GTK_ENTRY(keyentry), tmp );
        }
    }
#endif

    GtkWidget* content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    gtk_container_add( GTK_CONTAINER(content_area), hbox1 );
    gtk_container_add( GTK_CONTAINER(content_area), hbox2 );

    gtk_widget_show_all( dialog );

    int id = gtk_key_snooper_install( dialogkey_snooper, keyentry);
    int n = gtk_dialog_run(GTK_DIALOG(dialog));

    gtk_key_snooper_remove( id );

    if(pentry)
    {
        gchar *args =  (gchar*) gtk_entry_get_text( GTK_ENTRY(pentry));
        int np = check_format_string( args, vj_event_list[ info->uc.selected_vims_entry  ].format );

        if( np == vj_event_list[ info->uc.selected_vims_entry ].params )
        {
            if(info->uc.selected_vims_args )
                free(info->uc.selected_vims_args );

            info->uc.selected_vims_args = strdup( args );
        }
    }

    gtk_widget_destroy(dialog);

    return n;
}

void message_dialog( const char *title, char *msg )
{
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    GTK_WINDOW( mainw ),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_OK,
                                                    GTK_RESPONSE_NONE,
                                                    NULL);

    add_class(dialog, "reloaded" );

    GtkWidget *label = gtk_label_new( msg );
    g_signal_connect_swapped(dialog, "response",
                             G_CALLBACK(gtk_widget_destroy),dialog);
    GtkWidget* content_area = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    gtk_container_add(GTK_CONTAINER(content_area), label );
    gtk_widget_show_all(dialog);
}


int
prompt_dialog(const char *title, char *msg)
{
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    GTK_WINDOW( mainw ),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_NO,
                                                    GTK_RESPONSE_REJECT,
                                                    GTK_STOCK_YES,
                                                    GTK_RESPONSE_ACCEPT,
                                                    NULL);
    add_class( dialog, "reloaded" );

    gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_REJECT );
    gtk_window_set_resizable( GTK_WINDOW(dialog), FALSE );
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK( gtk_widget_hide ), G_OBJECT(dialog ) );
    GtkWidget *hbox1 = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 12 );
    gtk_container_set_border_width( GTK_CONTAINER( hbox1 ), 6 );
    GtkWidget *icon = gtk_image_new_from_stock( GTK_STOCK_DIALOG_QUESTION,
        GTK_ICON_SIZE_DIALOG );
    GtkWidget *label = gtk_label_new( msg );
    gtk_container_add( GTK_CONTAINER( hbox1 ), icon );
    gtk_container_add( GTK_CONTAINER( hbox1 ), label );
    gtk_container_add( GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), hbox1 );
    gtk_widget_show_all( dialog );

    int n = gtk_dialog_run(GTK_DIALOG(dialog));

    gtk_widget_destroy(dialog);

    return n;
}


int
error_dialog(const char *title, char *msg)
{
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window, "gveejay_window");
    GtkWidget *dialog = gtk_dialog_new_with_buttons(title,
                                                    GTK_WINDOW( mainw ),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_OK,
                                                    GTK_RESPONSE_OK,
                                                    NULL);
    add_class(dialog, "reloaded" );

    gtk_dialog_set_default_response( GTK_DIALOG(dialog), GTK_RESPONSE_OK );
    gtk_window_set_resizable( GTK_WINDOW(dialog), FALSE );
    g_signal_connect(G_OBJECT(dialog), "response",
                     G_CALLBACK( gtk_widget_hide ), G_OBJECT(dialog ) );
    GtkWidget *hbox1 = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 12 );
    gtk_container_set_border_width( GTK_CONTAINER( hbox1 ), 6 );
    GtkWidget *icon = gtk_image_new_from_stock(GTK_STOCK_DIALOG_ERROR,
                                               GTK_ICON_SIZE_DIALOG );
    GtkWidget *label = gtk_label_new( msg );
    gtk_container_add( GTK_CONTAINER( hbox1 ), icon );
    gtk_container_add( GTK_CONTAINER( hbox1 ), label );
    gtk_container_add( GTK_CONTAINER(gtk_dialog_get_content_area (GTK_DIALOG (dialog))), hbox1 );
    gtk_widget_show_all( dialog );

    int n = gtk_dialog_run(GTK_DIALOG(dialog));

    gtk_widget_destroy(dialog);

    return n;
}

void veejay_quit( )
{
    if( prompt_dialog("Quit veejay", "Close Veejay ? All unsaved work will be lost." )
         == GTK_RESPONSE_REJECT )
            return;
    single_vims( 600 );

//  clear_progress_bar( "cpumeter",0.0 );
    clear_progress_bar( "connecting",0.0 );
    clear_progress_bar( "samplerecord_progress",0.0 );
    clear_progress_bar( "streamrecord_progress",0.0 );
    clear_progress_bar( "seq_rec_progress",0.0);
    exit(0);
}

static int running_g_ = 1;
static int restart_   = 0;

int gveejay_restart()
{
    return restart_;
}

gboolean gveejay_running()
{
    if(!running_g_)
        return FALSE;
    return TRUE;
}

gboolean gveejay_relaunch()
{
    return (info->watch.state == STATE_QUIT ? FALSE: TRUE);
}

gboolean gveejay_quit( GtkWidget *widget, gpointer user_data)
{
    if( info->watch.state == STATE_PLAYING)
    {
        if(prompt_dialog("Quit Reloaded", "Are you sure?") != GTK_RESPONSE_ACCEPT)
            return TRUE;
    }

    info->watch.state = STATE_QUIT;

    return FALSE;
}

/* Free the slot */
static void free_slot( sample_slot_t *slot )
{
    if(slot)
    {
        if(slot->title) free(slot->title);
        if(slot->timecode) free(slot->timecode);
        free(slot);
    }
    slot = NULL;
}

/* Allocate some memory and create a temporary slot */
sample_slot_t   *create_temporary_slot( gint slot_id, gint id, gint type, gchar *title, gchar *timecode )
{
    sample_slot_t *slot = (sample_slot_t*) vj_calloc(sizeof(sample_slot_t));
    if(id>0)
    {
        slot->sample_id = id;
        slot->sample_type = type;
        slot->timecode = strduplastn(timecode);
        //slot->title = strduplastn(title);
	slot->title = strdup(title);
        slot->slot_number = slot_id;
    }
    return slot;
}

int is_current_track(char *host, int port )
{
    char *remote = get_text( "entry_hostname" );
    int num  = get_nums( "button_portnum" );
    if( strncasecmp( remote, host, strlen(host)) == 0 && port == num )
        return 1;
    return 0;
}

void gveejay_popup_err( const char *type, char *msg )
{
    message_dialog( type, msg );
}

void donatenow();
void update_gui();

int veejay_get_sample_image(int id, int type, int wid, int hei)
{
    multi_vims( VIMS_GET_SAMPLE_IMAGE, "%d %d %d %d", id, type, wid, hei );
    uint8_t *data_buffer = (uint8_t*) vj_malloc( sizeof(uint8_t) * RUP8(wid * hei * 3));
    int sample_id = 0;
    int sample_type =0;
    gint bw = 0;
    gchar *data = recv_vims_args( 12, &bw, &sample_id, &sample_type );
    if( data == NULL || bw <= 0 )
    {
        if( data_buffer )
            free(data_buffer);
        if( data )
            free(data);
        return 0;
    }

    int expected_len = (wid * hei);
    expected_len += (wid*hei/4);
    expected_len += (wid*hei/4);

    if( bw != expected_len )
    {
        if(data_buffer)
            free(data_buffer);
        if( data )
            free(data);
        return 0;
    }

    uint8_t *in = (uint8_t*)data;
    uint8_t *out = data_buffer;

    VJFrame *src1 = yuv_yuv_template( in, in + (wid * hei), in + (wid * hei) + (wid*hei)/4,wid,hei,PIX_FMT_YUV420P );
    VJFrame *dst1 = yuv_rgb_template( out, wid,hei,PIX_FMT_BGR24 );

    yuv_convert_any_ac( src1, dst1, src1->format, dst1->format );

    GdkPixbuf *img = gdk_pixbuf_new_from_data(out,
                                              GDK_COLORSPACE_RGB,
                                              FALSE,
                                              8,
                                              wid,
                                              hei,
                                              wid*3,
                                              NULL,
                                              NULL );

    if( img == NULL )
        return 0;

/*  int poke_slot= 0; int bank_page = 0;
    verify_bank_capacity( &bank_page , &poke_slot, sample_id, sample_type);
    if(bank_page >= 0 )
    {
        if( info->sample_banks[bank_page]->slot[poke_slot]->sample_id <= 0 )
        {
            sample_slot_t *tmp_slot = create_temporary_slot(poke_slot,sample_id,sample_type, "PREVIEW","00:00:00" );
            add_sample_to_sample_banks(bank_page, tmp_slot );
            free_slot(tmp_slot);
}
    } */

    sample_slot_t *slot = find_slot_by_sample( sample_id, sample_type );
    sample_gui_slot_t *gui_slot = find_gui_slot_by_sample( sample_id, sample_type );

    if( slot && gui_slot )
    {
        slot->pixbuf = vj_gdk_pixbuf_scale_simple(img,wid,hei, GDK_INTERP_NEAREST);
        if( slot->pixbuf)
        {
            gtk_image_set_from_pixbuf_( GTK_IMAGE( gui_slot->image ), slot->pixbuf );
            g_object_unref( slot->pixbuf );
            slot->pixbuf = NULL;
        }
    }

    free(data_buffer);
    free(data);
    g_object_unref(img);

    free(src1);
    free(dst1);


    return bw;
}

void gveejay_new_slot(int mode)
{
    if(!samplebank_ready_) {
        samplebank_ready_ = 1;
    }
    if( mode == MODE_STREAM ) {
        info->uc.expected_num_streams = info->uc.real_num_streams + 1;
    }
    else {
        info->uc.expected_num_samples = info->uc.real_num_samples + 1;
    }
}

#include "callback.c"
enum
{
    COLOR_RED=0,
    COLOR_BLUE=1,
    COLOR_GREEN=2,
    COLOR_BLACK=3,
    COLOR_NUM
};

void vj_msg(int type, const char format[], ...)
{
    if( type == VEEJAY_MSG_DEBUG && vims_verbosity == 0 )
        return;

    char tmp[1024];
    char buf[1024];
    char prefix[20];
    va_list args;

    va_start( args,format );
    vsnprintf( tmp, sizeof(tmp), format, args );

    switch(type)
    {
        case 2:
            sprintf(prefix,"Info:");
            break;
        case 1:
            sprintf(prefix,"Warning:");
            break;
        case 0:
            sprintf(prefix,"Error:" );
            break;
        default:
            sprintf(prefix,"Debug:");
            break;
    }

    snprintf(buf, sizeof(buf), "%s %s\n",prefix,tmp );
    gsize nr,nw;
    gchar *text = g_locale_to_utf8( buf, -1, &nr, &nw, NULL);
    text[strlen(text)-1] = '\0';

    gtk_statusbar_push( GTK_STATUSBAR( widget_cache[ WIDGET_STATUSBAR] ),0, text );

    g_free( text );
    va_end(args);
}

void msg_vims(char *message)
{
    if(!info->client)
        return;
    int n = vj_client_send(info->client, V_CMD, (unsigned char*)message);
    if( n <= 0 )
        reloaded_schedule_restart();
}

int get_loop_value()
{
    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_LOOP_NONE]) ) )
       return 0; 
    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_LOOP_NORMAL]) ) )
       return 1; 
    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_LOOP_PINGPONG]) ) )
       return 2; 
    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_LOOP_RANDOM]) ) )
       return 3; 
    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_LOOP_ONCENOP]) ) )
       return 4; 

    return 1; // loop normal
}

static void multi_vims(int id, const char format[],...)
{
    char block[1032];
    char tmp[1024];
    va_list args;
    if(!info->client)
        return;
    va_start(args, format);
    vsnprintf(tmp, sizeof(tmp), format, args );
    snprintf(block, sizeof(block), "%03d:%s;",id,tmp);
    va_end(args);

    if(vj_client_send( info->client, V_CMD, (unsigned char*) block)<=0 )
        reloaded_schedule_restart();
}

static void single_vims(int id)
{
    char block[8];
    if(!info->client)
        return;
    snprintf(block,sizeof(block), "%03d:;",id);
    if(vj_client_send( info->client, V_CMD, (unsigned char*) block)<=0 )
        reloaded_schedule_restart();
}

static gchar *recv_vims_args(int slen, int *bytes_written, int *arg0, int *arg1)
{
    int tmp_len = slen+1;
    unsigned char tmp[tmp_len];
    veejay_memset(tmp,0,sizeof(tmp));
    int ret = vj_client_read( info->client, V_CMD, tmp, slen );
    if( ret == -1 )
        reloaded_schedule_restart();
    int len = 0;
    if( sscanf( (char*)tmp, "%06d", &len ) != 1 )
        return NULL;
    if( sscanf( (char*)tmp + 6, "%04d", arg0 ) != 1 )
        return NULL;
    if( sscanf( (char*)tmp + 10, "%02d", arg1) != 1 )
        return NULL;
    unsigned char *result = NULL;
    if( ret <= 0 || len <= 0 || slen <= 0)
        return (gchar*)result;
    result = (unsigned char*) vj_calloc(sizeof(unsigned char) * RUP8(len + 1 + 16) );
    *bytes_written = vj_client_read( info->client, V_CMD, result, len );
    if( *bytes_written == -1 )
        reloaded_schedule_restart();

    return (gchar*) result;
}

static gchar *recv_vims(int slen, int *bytes_written)
{
    int tmp_len = slen+1;
    unsigned char tmp[tmp_len];
    veejay_memset(tmp,0,sizeof(tmp));
    int ret = vj_client_read( info->client, V_CMD, tmp, slen );
    if( ret == -1 )
        reloaded_schedule_restart();
    int len = 0;
    if( sscanf( (char*)tmp, "%d", &len ) != 1 )
        return NULL;
    unsigned char *result = NULL;
    if( ret <= 0 || len <= 0 || slen <= 0)
        return (gchar*)result;
    result = (unsigned char*) vj_calloc(sizeof(unsigned char) * (len + 1) );
    *bytes_written = vj_client_read( info->client, V_CMD, result, len );
    if( *bytes_written == -1 )
        reloaded_schedule_restart();
    return (gchar*) result;
}

static gdouble  get_numd(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
    if(!w) return 0;
    return (gdouble) gtk_spin_button_get_value( GTK_SPIN_BUTTON( w ) );
}

static int get_slider_val2(GtkWidget *w) 
{
    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    return (int) gtk_adjustment_get_value (a);
}

static int get_slider_val(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w) return 0;
    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    return (int) gtk_adjustment_get_value (a);
}

static void vj_kf_reset()
{
    int osl = info->status_lock;

    info->status_lock = 1;
    reset_curve( info->curve );

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM]))) {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM]), FALSE );
    }

    /* update the time bounds accordingly the sample marker*/
    int total_frames = (info->status_tokens[PLAY_MODE] == MODE_STREAM ? 
                        info->status_tokens[SAMPLE_MARKER_END] : 
                        ( info->status_tokens[PLAY_MODE] == MODE_SAMPLE ? info->status_tokens[TOTAL_FRAMES]: 0) );
    int lo = (info->selection[0] == info->selection[1] ? 0 : info->selection[0]);
    int hi = (info->selection[1] > info->selection[0] ? info->selection[1] : total_frames );
    if( lo == hi ) {
        lo = 0;
        hi = total_frames;
    }
    update_spin_range2( widget_cache[WIDGET_CURVE_SPINSTART], 0, total_frames, lo );
    update_spin_range2( widget_cache[WIDGET_CURVE_SPINEND], 0, total_frames, hi );

    GtkWidget* curveparam = widget_cache[WIDGET_COMBO_CURVE_FX_PARAM];
    //block "changed" signal to prevent propagation
    guint signal_id=g_signal_lookup("changed", GTK_TYPE_COMBO_BOX);
    gulong handler_id=handler_id=g_signal_handler_find( (gpointer)curveparam,
                                                        G_SIGNAL_MATCH_ID,
                                                        signal_id,
                                                        0, NULL, NULL, NULL );
    if (handler_id) g_signal_handler_block((gpointer)curveparam, handler_id);
    gtk_combo_box_set_active (GTK_COMBO_BOX(curveparam),0);
    if (handler_id) g_signal_handler_unblock((gpointer)curveparam, handler_id);

    info->status_lock = osl;
}

static void vj_kf_refresh(gboolean force)
{
    if(!force && !is_fxanim_displayed())
        return;

    int *entry_tokens = &(info->uc.entry_tokens[0]);

    if( entry_tokens[ENTRY_FXID] > 0 && _effect_get_np( entry_tokens[ENTRY_FXID] )) {
        gtk_widget_set_sensitive_( widget_cache[WIDGET_FRAME_FXTREE3] , TRUE );

        GtkWidget* curveparam = widget_cache[WIDGET_COMBO_CURVE_FX_PARAM];
        //block "changed" signal to prevent propagation
        guint signal_id=g_signal_lookup("changed", GTK_TYPE_COMBO_BOX);
        gulong handler_id=handler_id=g_signal_handler_find( (gpointer)curveparam,
                                                            G_SIGNAL_MATCH_ID,
                                                            signal_id,
                                                            0, NULL, NULL, NULL );
        if (handler_id) g_signal_handler_block((gpointer)curveparam, handler_id);
        gtk_combo_box_set_active (GTK_COMBO_BOX(curveparam),0);
        if (handler_id) g_signal_handler_unblock((gpointer)curveparam, handler_id);

        vj_kf_select_parameter(0);
    } else {
        vj_kf_reset();
        gtk_widget_set_sensitive_( widget_cache[WIDGET_FRAME_FXTREE3], FALSE );
    }
}

/*! \brief Reset the current curve and update using the selected effect parameter
 *
 *  \sa reset_curve
 *  \sa update_curve_widget
 *
 *  \param num the selected effect parameter
 */
static void vj_kf_select_parameter(int num)
{
    sample_slot_t *s = info->selected_slot;
    if(!s) {
        gtk_combo_box_set_active (GTK_COMBO_BOX(widget_cache[WIDGET_COMBO_CURVE_FX_PARAM]),FALSE);
        return;
    }

    info->uc.selected_parameter_id = num;
    reset_curve( info->curve );
    update_curve_widget( info->curve );
}

/*! \brief Ask server KF of current fx param and and set them. If none set initial value
 *
 *  \sa set_points_in_curve_ext
 *  \sa set_initial_curve
 *
 *  \param num the selected effect parameter
 */
static void update_curve_widget(GtkWidget *curve)
{
    int i = info->uc.selected_chain_entry; /* chain entry */
    int id = info->uc.entry_tokens[ENTRY_FXID];
    int blen = 0;
    int lo = 0, hi = 0, curve_type=0;
    int p = -1;
    int status = 0;

    multi_vims( VIMS_SAMPLE_KF_GET, "%d %d",i,info->uc.selected_parameter_id );

    unsigned char *blob = (unsigned char*) recv_vims( 8, &blen );
    int checksum = data_checksum( (char*) blob, blen ) + i + info->uc.selected_parameter_id;

    if( info->uc.reload_hint_checksums[HINT_KF] == checksum ) {
        if( blob ) free(blob);
        return;
    }
    info->uc.reload_hint_checksums[HINT_KF] = checksum;

    /* update the time bounds accordingly the sample marker*/
    if( lo == hi && hi == 0 )
    {
        if( info->status_tokens[PLAY_MODE] == MODE_SAMPLE ) {
            lo = info->status_tokens[SAMPLE_START];
            hi = info->status_tokens[SAMPLE_END];
        } else {
            lo = 0;
            hi = info->status_tokens[SAMPLE_MARKER_END];
        }
    }
    int total_frames = (info->status_tokens[PLAY_MODE] == MODE_STREAM ? 
                        info->status_tokens[SAMPLE_MARKER_END] : 
                        ( info->status_tokens[PLAY_MODE] == MODE_SAMPLE ? info->status_tokens[TOTAL_FRAMES]: 0) );
    update_spin_range2( widget_cache[WIDGET_CURVE_SPINSTART],0, total_frames, lo );
    update_spin_range2( widget_cache[WIDGET_CURVE_SPINEND], 0, total_frames, hi );

    /* If parameter have KF set the points or set the initial curve */
    if( blob && blen > 0 )
    {
        p = set_points_in_curve_ext( curve, blob,id,i, &lo,&hi, &curve_type,&status );
        if( p >= 0 )
        {
            info->uc.selected_parameter_id = p;
            switch( curve_type )
            {
                case GTK3_CURVE_TYPE_SPLINE:
                    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPESPLINE]), TRUE );
                    break;
                case GTK3_CURVE_TYPE_FREE:
                    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPEFREEHAND]), TRUE);
                    break;
                default: 
                    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPELINEAR]), TRUE );
                    break;
            }
            if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM])) != status ) {
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM]), status );
            }
        }
    } else {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TOGGLEENTRY_PARAM]), FALSE );
        set_initial_curve( curve, info->uc.entry_tokens[ENTRY_FXID], info->uc.selected_parameter_id,
                           lo, hi ,
                           info->uc.entry_tokens[ ENTRY_P0 + info->uc.selected_parameter_id ] );
    }

    if(blob) free(blob);

    return;
}

static int get_nums(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (spin): '%s'",name);
        return 0;
    }
    return (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON( w ) );
}

static int count_textview_buffer(const char *name)
{
    GtkWidget *view = glade_xml_get_widget_( info->main_window, name );
    if(view)
    {
        GtkTextBuffer *tb = NULL;
        tb = gtk_text_view_get_buffer( GTK_TEXT_VIEW(view) );
        return gtk_text_buffer_get_char_count( tb );
    }
    return 0;
}

static void clear_textview_buffer(const char *name)
{
    GtkWidget *view = glade_xml_get_widget_( info->main_window, name );
    if(!view) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (textview): '%s'",name);
        return;
    }
    if(view)
    {
        GtkTextBuffer *tb = NULL;
        tb = gtk_text_view_get_buffer( GTK_TEXT_VIEW(view) );
        GtkTextIter iter1,iter2;
        gtk_text_buffer_get_start_iter( tb, &iter1 );
        gtk_text_buffer_get_end_iter( tb, &iter2 );
        gtk_text_buffer_delete( tb, &iter1, &iter2 );
    }
}

static gchar *get_textview_buffer(const char *name)
{
    GtkWidget *view = glade_xml_get_widget_( info->main_window,name );
    if(!view) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (textview): '%s'",name);
        return NULL;
    }
    if(view)
    {
        GtkTextBuffer *tb = NULL;
        tb = gtk_text_view_get_buffer( GTK_TEXT_VIEW(view) );
        GtkTextIter iter1,iter2;

        gtk_text_buffer_get_start_iter(tb, &iter1);
        gtk_text_buffer_get_end_iter( tb, &iter2);
        gchar *res = gtk_text_buffer_get_text( tb, &iter1,&iter2 , TRUE );
        return res;
    }
    return NULL;
}

static void set_textview_buffer(const char *name, gchar *utf8text)
{
    GtkWidget *view = glade_xml_get_widget_( info->main_window, name );
    if(!view) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (textview): '%s'",name);
        return;
    }
    if(view)
    {
        GtkTextBuffer *tb = gtk_text_view_get_buffer(
                    GTK_TEXT_VIEW(view) );
        gtk_text_buffer_set_text( tb, utf8text, -1 );
    }
}

static gchar *get_text(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (text): '%s'",name);
        return NULL;
    }
    return (gchar*) gtk_entry_get_text( GTK_ENTRY(w));
}

static void put_text(const char *name, char *text)
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (text): '%s'",name);
        return;
    }
    if(w)
    {
        gchar *utf8_text = _utf8str( text );
        gtk_entry_set_text( GTK_ENTRY(w), utf8_text );
        g_free(utf8_text);
    }
}

int is_button_toggled(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name);
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (togglebutton): '%s'",name);
        return 0;
    }

    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(w) ) == TRUE )
        return 1;
    return 0;
}
static void set_toggle_button(const char *name, int status)
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (togglebutton): '%s'",name);
        return;
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (status==1 ? TRUE: FALSE));
}


static void update_slider_gvalue(const char *name, gdouble value)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (slider): '%s'",name);
        return;
    }
    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    gtk_adjustment_set_value( a, value );
}

static void update_slider_value2(GtkWidget *w, gint value, gint scale) 
{
    gdouble gvalue;
    if(scale)
        gvalue = (gdouble) value / (gdouble) scale;
    else
        gvalue = (gdouble) value;

    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    gtk_adjustment_set_value( a, gvalue );
}

static void update_slider_value(const char *name, gint value, gint scale)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (slider): '%s'",name);
        return;
    }
    gdouble gvalue;
    if(scale)
        gvalue = (gdouble) value / (gdouble) scale;
    else
        gvalue = (gdouble) value;

    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    gtk_adjustment_set_value( a, gvalue );
}

static void update_spin_incr( const char *name, gdouble step, gdouble page )
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (spin): '%s'",name);
        return;
    }
    
    gtk_spin_button_set_increments(GTK_SPIN_BUTTON(w),step,page );
}

static void update_spin_range2(GtkWidget *w, gint min, gint max, gint val)
{
    gtk_spin_button_set_range( GTK_SPIN_BUTTON(w), (gdouble)min, (gdouble) max );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble)val);
}

static void update_spin_range(const char *name, gint min, gint max, gint val)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (spin): '%s'",name);
        return;
    }
    gtk_spin_button_set_range( GTK_SPIN_BUTTON(w), (gdouble)min, (gdouble) max );
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble)val);
}

/*static int get_mins(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w) return 0;
    GtkAdjustment *adj = gtk_spin_button_get_adjustment( GTK_SPIN_BUTTON(w) );
    return (int) adj->lower;
}

static int get_maxs(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w) return 0;
    GtkAdjustment *adj = gtk_spin_button_get_adjustment( GTK_SPIN_BUTTON(w) );
    return (int) adj->upper;
}*/

static void update_spin_value(const char *name, gint value )
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (spin): '%s'",name);
        return;
    }
    
    gtk_spin_button_set_value( GTK_SPIN_BUTTON(w), (gdouble) value );
}
static void update_slider_range2(GtkWidget *w, gint min, gint max, gint value, gint scaled)
{
    if(min == max) {
        return;
    }

    GtkRange *range = GTK_RANGE(w);
    if(!scaled)
    {
        gtk_range_set_range(range, (gdouble) min, (gdouble) max );
        gtk_range_set_value(range, value );
    }
    else
    {
        gdouble gmin =0.0;
        gdouble gmax =100.0;
        gdouble gval = gmax / value;
        gtk_range_set_range(range, gmin, gmax);
        gtk_range_set_value(range, gval );
    }

    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    gtk_range_set_adjustment(range, a );
}

static void update_slider_range(const char *name, gint min, gint max, gint value, gint scaled)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window, name );
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (slider): '%s'",name);
        return;
    }

    if(min == max) {
	return;
    }

    GtkRange *range = GTK_RANGE(w);
    if(!scaled)
    {
        gtk_range_set_range(range, (gdouble) min, (gdouble) max );
        gtk_range_set_value(range, value );
    }
    else
    {
        gdouble gmin =0.0;
        gdouble gmax =100.0;
        gdouble gval = gmax / value;
        gtk_range_set_range(range, gmin, gmax);
        gtk_range_set_value(range, gval );
    }

    GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( w ));
    gtk_range_set_adjustment(range, a );
}

static void update_label_i2(GtkWidget *label, int num, int prefix)
{
    char str[20];
    if(prefix)
        g_snprintf( str,sizeof(str), "%09d", num );
    else
        g_snprintf( str,sizeof(str), "%d",    num );
    gtk_label_set_text( GTK_LABEL(label), str);
}

static void update_label_i(const char *name, int num, int prefix)
{
    GtkWidget *label = glade_xml_get_widget_(
                info->main_window, name);
    if(!label) {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (label): '%s'",name);
        return;
    }
    char str[20];
    if(prefix)
        g_snprintf( str,sizeof(str), "%09d", num );
    else
        g_snprintf( str,sizeof(str), "%d",    num );
    gchar *utf8_value = _utf8str( str );
    gtk_label_set_text( GTK_LABEL(label), utf8_value);
    g_free( utf8_value );
}

static void update_label_f(const char *name, float val )
{
    GtkWidget *label = glade_xml_get_widget_( info->main_window, name);
    if(!label)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "No such widget (label): '%s'",name);
        return;
    }
    char value[10];
    snprintf( value, sizeof(value)-1, "%2.2f", val );

    gchar *utf8_value = _utf8str( value );
    gtk_label_set_text( GTK_LABEL(label), utf8_value );
    g_free(utf8_value);
}

static void update_label_str(const char *name, gchar *text)
{
    GtkWidget *label = glade_xml_get_widget_(
                info->main_window, name);

#ifdef STRICT_CHECKING
    if(!label) veejay_msg(VEEJAY_MSG_ERROR, "No such widget (label): '%s'",name);
    assert( label != NULL );
#else
    if(!label ||!text) return;
#endif
    gchar *utf8_text = _utf8str( text );
    if(!utf8_text) return;
    gtk_label_set_text( GTK_LABEL(label), utf8_text);
    g_free(utf8_text);
}

static void label_set_markup(const char *name, gchar *str)
{
    GtkWidget *label = glade_xml_get_widget_(
                info->main_window, name);
    if(!label)
        return;

    gtk_label_set_markup( GTK_LABEL(label), str );
}

static void selection_get_paths(GtkTreeModel *model,
                                GtkTreePath *path,
                                GtkTreeIter *iter, gpointer data)
{
    GSList **paths = data;
    *paths = g_slist_prepend(*paths, gtk_tree_path_copy(path));
}

GSList *gui_tree_selection_get_paths(GtkTreeView *view)
{
    GtkTreeSelection *sel;
    GSList *paths;

    /* get paths of selected rows */
    paths = NULL;
    sel = gtk_tree_view_get_selection(view);
    gtk_tree_selection_selected_foreach(sel, selection_get_paths, &paths);

    return paths;
}

static void update_colorselection()
{
    GtkWidget *colorsel = glade_xml_get_widget_(info->main_window,
                                                "colorselection");
    GdkColor color;

    color.red = 255 * info->status_tokens[STREAM_COL_R];
    color.green = 255 * info->status_tokens[STREAM_COL_G];
    color.blue = 255 * info->status_tokens[STREAM_COL_B];

    gtk_color_selection_set_current_color(GTK_COLOR_SELECTION( colorsel ),
                                          &color );
}

int resize_primary_ratio_y()
{
    float ratio = (float)info->el.width / (float)info->el.height;
    float result = (float) get_nums( "priout_width" ) / ratio;
    return (int) result;
}

int resize_primary_ratio_x()
{
    float ratio = (float)info->el.height / (float)info->el.width;
    float result = (float) get_nums( "priout_height" ) / ratio;
    return (int) result;
}

static void update_rgbkey()
{
    if(!info->entry_lock)
    {
        info->entry_lock =1;
        GtkWidget *colorsel = widget_cache[ WIDGET_RGBKEY ];
        GdkColor color;
        /* update from entry tokens (delivered by GET_CHAIN_ENTRY */
        int *p = &(info->uc.entry_tokens[0]);
        
        color.red = 255 * p[ENTRY_P1];
        color.green = 255 * p[ENTRY_P2];
        color.blue = 255 * p[ENTRY_P3];

        gtk_color_selection_set_current_color(
            GTK_COLOR_SELECTION( colorsel ),
            &color );
        info->entry_lock = 0;
    }
}

static void update_rgbkey_from_slider()
{
    if(!info->entry_lock)
    {
        GtkWidget *colorsel = widget_cache[ WIDGET_RGBKEY ];
        info->entry_lock = 1;
        GdkColor color;

        color.red = 255 * ( get_slider_val2( widget_cache[WIDGET_SLIDER_P1]) );
        color.green = 255 * ( get_slider_val2( widget_cache[WIDGET_SLIDER_P2]) );
        color.blue = 255 * ( get_slider_val2( widget_cache[WIDGET_SLIDER_P3]) );

        gtk_color_selection_set_current_color(GTK_COLOR_SELECTION( colorsel ),
                                              &color );
        info->entry_lock = 0;
    }
}

static  GdkPixbuf   *update_pixmap_entry( int status )
{
    char path[MAX_PATH_LEN];
    char filename[MAX_PATH_LEN];

    snprintf(filename,sizeof(filename), "fx_entry_%s.png",
             ( status == 1 ? "on" : "off" ));
    get_gd(path,NULL, filename);

    GError *error = NULL;
    GdkPixbuf *icon = gdk_pixbuf_new_from_file(path, &error);
    if(error)
        return 0;
    return icon;
}

static gboolean chain_update_row(GtkTreeModel * model,
                                 GtkTreePath * path,
                                 GtkTreeIter * iter,
                                 gpointer data)
{
    vj_gui_t *gui = (vj_gui_t*) data;
    if(!gui->selected_slot)
        return FALSE;
    int entry = info->uc.selected_chain_entry;
    gint gentry = 0;

    gtk_tree_model_get (model, iter,FXC_ID, &gentry, -1);

    if(gentry == entry)
    {
        int effect_id = gui->uc.entry_tokens[ ENTRY_FXID ];
        if( effect_id <= 0 )
        {
            gtk_list_store_set( GTK_LIST_STORE(model),iter, FXC_ID, entry, -1 );
        }
        else
        {
            gchar *descr = _utf8str( _effect_get_description( effect_id ));
            char  tmp[128];
            if( _effect_get_mix( effect_id ) )
            {
                snprintf(tmp,sizeof(tmp),"%s %d", (gui->uc.entry_tokens[ENTRY_SOURCE] == 0 ? "Sample " : "T " ),
                    gui->uc.entry_tokens[ENTRY_CHANNEL]);
            }
            else
            {
                snprintf(tmp,sizeof(tmp),"%s"," ");
            }

            gchar *mixing = _utf8str(tmp);
            int kf_status = gui->uc.entry_tokens[ENTRY_KF_STATUS];
            GdkPixbuf *toggle = update_pixmap_entry( gui->uc.entry_tokens[ENTRY_VIDEO_ENABLED] );
            GdkPixbuf *kf_toggle = update_pixmap_entry( kf_status );
            GdkPixbuf *subrender_toggle = update_pixmap_entry( gui->uc.entry_tokens[ENTRY_SUBRENDER_ENTRY]);
            gtk_list_store_set( GTK_LIST_STORE(model),iter,
                               FXC_ID, entry,
                               FXC_FXID, descr,
                               FXC_FXSTATUS, toggle,
                               FXC_KF, kf_toggle,
                               FXC_MIXING, mixing,
                               FXC_SUBRENDER, subrender_toggle,
                               FXC_KF_STATUS, kf_status,
                               -1 );
            g_free(descr);
            g_free(mixing);
            g_object_unref( kf_toggle );
            g_object_unref( toggle );
            g_object_unref( subrender_toggle );
        }
    }
    return FALSE;
}

/* Cut from global_info()
   This function updates the sample/stream editor if the current playing stream/sample
   matches with the selected sample slot */
static void update_record_tab(int pm)
{
    if(pm == MODE_STREAM)
    {
        update_spin_value( "spin_streamduration" , 1 );
        gint n_frames = get_nums( "spin_streamduration" );
        char *time = format_time(n_frames, (double) info->el.fps);
        update_label_str( "label_streamrecord_duration", time );
        free(time);
    }
    if(pm == MODE_SAMPLE)
    {
        on_spin_sampleduration_value_changed(NULL,NULL);
    }
}

static void update_current_slot(int *history, int pm, int last_pm)
{
    gint update = 0;

    if( pm != last_pm || info->status_tokens[CURRENT_ID] != history[CURRENT_ID] )
    {
        int k;
        update = 1;
        update_record_tab( pm );

        if( info->status_tokens[STREAM_TYPE] == STREAM_VIDEO4LINUX )
        {
            info->uc.reload_hint[HINT_V4L] = 1;
            for(k = 0; capt_card_set[k].name != NULL; k ++ )
            {
                show_widget( capt_card_set[k].name );
                show_widget( capt_label_set[k].name );
            }
        }
        else
        {
            for(k = 0; capt_card_set[k].name != NULL ; k ++ )
            {
                hide_widget( capt_card_set[k].name );
                hide_widget( capt_label_set[k].name );
            }
        }

        switch(info->status_tokens[STREAM_TYPE])
        {
            case STREAM_GENERATOR:
                    if(!gtk_widget_is_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ) ) ) {
                        gtk_widget_set_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ), TRUE );
                    }
                    show_widget("frame_fxtree1");
                    hide_widget("notebook16");
                    break;
            case STREAM_WHITE:
                    hide_widget("frame_fxtree1");
                    if(gtk_widget_is_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ) ) ) {
                        gtk_widget_set_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ), FALSE );
                    }

                    show_widget("notebook16");
                    notebook_set_page("notebook16",1 );
                    break;
            case STREAM_VIDEO4LINUX:
                    hide_widget("frame_fxtree1");
                    if(gtk_widget_is_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ) ) ) {
                        gtk_widget_set_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ), FALSE );
                    }

                    show_widget("notebook16");
                    notebook_set_page("notebook16",0 );
                    break;
            default:
                    hide_widget( "frame_fxtree1");
                    if(gtk_widget_is_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ) ) ) {
                        gtk_widget_set_sensitive( GTK_WIDGET( widget_cache[WIDGET_FRAME_FXTREE1] ), FALSE );
                    }
                    hide_widget( "notebook16");
                    break;
        }

        info->uc.reload_hint[HINT_HISTORY] = 1;
        info->uc.reload_hint[HINT_CHAIN] = 1;
        info->uc.reload_hint[HINT_ENTRY] = 1;
        info->uc.reload_hint[HINT_KF] = 1;

        put_text( "entry_samplename", "" );
        set_pm_page_label( info->status_tokens[CURRENT_ID], pm );

    }

    /* Actions for stream */
    if( ( info->status_tokens[CURRENT_ID] != history[CURRENT_ID] || pm != last_pm ) && pm == MODE_STREAM )
    {
        /* Is a solid color stream */
        if( info->status_tokens[STREAM_TYPE] == STREAM_WHITE )
        {
            if( ( history[STREAM_COL_R] != info->status_tokens[STREAM_COL_R] ) ||
                ( history[STREAM_COL_G] != info->status_tokens[STREAM_COL_G] ) ||
                ( history[STREAM_COL_B] != info->status_tokens[STREAM_COL_B] ) )
             {
                info->uc.reload_hint[HINT_RGBSOLID] = 1;
             }
        }

        if( info->status_tokens[STREAM_TYPE] == STREAM_GENERATOR )
        {
            info->uc.reload_hint[HINT_GENERATOR] = 1;
        }

        /*char *time = format_time( info->status_frame,(double)info->el.fps );
        update_label_str( "label_curtime", time );
        free(time);*/
        
        update_label_str( "playhint", "Streaming");
    
        info->uc.reload_hint[HINT_KF] = 1;
    }

    /* Actions for sample */
    if( pm == MODE_SAMPLE )
    {
        int marker_go = 0;
        /* Update marker bounds */
        if( (history[SAMPLE_MARKER_START] != info->status_tokens[SAMPLE_MARKER_START]) )
        {
            update = 1;
            gint nm =  info->status_tokens[SAMPLE_MARKER_START];
            if(nm >= 0)
            {
                gdouble in = (1.0 / (gdouble)info->status_tokens[TOTAL_FRAMES]) * nm;
                timeline_set_in_point( info->tl, in );
                marker_go = 1;
            }
            else
            {
                if(pm == MODE_SAMPLE)
                {
                    timeline_set_in_point( info->tl, 0.0 );
                    marker_go = 1;
                }
            }
            char *dur = format_time( info->status_tokens[SAMPLE_MARKER_END] - info->status_tokens[SAMPLE_MARKER_START],
                (double)info->el.fps );
            char *start = format_time( info->status_tokens[SAMPLE_MARKER_START] , (double)info->el.fps);
            update_label_str( "label_markerduration", dur );
            update_label_str( "label_markerstart", start );
            free(dur);
            free(start);
        }

        if( (history[SAMPLE_MARKER_END] != info->status_tokens[SAMPLE_MARKER_END]) )
        {
            gint nm = info->status_tokens[SAMPLE_MARKER_END];
            if(nm > 0 )
            {
                gdouble out = (1.0/ (gdouble)info->status_tokens[TOTAL_FRAMES]) * nm;

                timeline_set_out_point( info->tl, out );
                marker_go = 1;
            }
            else
            {
                if(pm == MODE_SAMPLE)
                {
                    timeline_set_out_point(info->tl, 1.0 );
                    marker_go = 1;
                }
            }

            char *end = format_time( info->status_tokens[SAMPLE_MARKER_END], (double)info->el.fps );
            gtk_label_set_text( GTK_LABEL(widget_cache[WIDGET_LABEL_MARKEREND]), end);
            free(end);

            update = 1;
        }

        if( marker_go )
        {
            info->uc.reload_hint[HINT_MARKER] = 1;
        }

        if( history[SAMPLE_LOOP] != info->status_tokens[SAMPLE_LOOP])
        {
            switch( info->status_tokens[SAMPLE_LOOP] )
            {
                case 0:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_LOOP_NONE]), TRUE); 
                break;
                case 1:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_LOOP_NORMAL]), TRUE); 
                break;
                case 2:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_LOOP_PINGPONG]), TRUE); 
                break;
                case 3:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_LOOP_RANDOM]), TRUE); 
                break;
                case 4:
                    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_LOOP_ONCENOP]), TRUE); 
                    break;
            }
        }

        gint speed = info->status_tokens[SAMPLE_SPEED];

        if( history[SAMPLE_SPEED] != info->status_tokens[SAMPLE_SPEED] )
        {
            speed = info->status_tokens[SAMPLE_SPEED];
            update_slider_value( "speed_slider", speed, 0 );

            if( speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
            if( speed < 0 ) speed *= -1;

            gtk_spin_button_set_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_SPIN_SAMPLESPEED]), (gdouble) speed );

            if( pm == MODE_SAMPLE ) {
                if( speed == 0 )
                    update_label_str( "playhint", "Paused" );
                else
                    update_label_str( "playhint", "Playing");
            }
        }

        if( history[FRAME_DUP] != info->status_tokens[FRAME_DUP] )
        {
            update_spin_value( "spin_framedelay", info->status_tokens[FRAME_DUP]);
            update_slider_value("slow_slider", info->status_tokens[FRAME_DUP],0);
        }

        /* veejay keeps sample limits , dont use update_spin_range for spin_samplestart and spin_sampleend */
        if( (history[SAMPLE_START] != info->status_tokens[SAMPLE_START] || get_nums("spin_samplestart") != info->status_tokens[SAMPLE_START]))
        {
            update = 1;
        }
        if( (history[SAMPLE_END] != info->status_tokens[SAMPLE_END] || get_nums("spin_sampleend") != info->status_tokens[SAMPLE_END]) )
        {
            update = 1;
        }

        if( (history[SUBRENDER] != info->status_tokens[SUBRENDER] || gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget_cache[ WIDGET_TOGGLE_SUBRENDER ]) ) != info->status_tokens[SUBRENDER]) )
        {
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[ WIDGET_TOGGLE_SUBRENDER ] ) , TRUE );
        }

        if( history[FADE_ALPHA] != info->status_tokens[FADE_ALPHA] || info->status_tokens[FADE_ALPHA] != gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[ WIDGET_TOGGLE_FADEMETHOD ] )) )
        {
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[ WIDGET_TOGGLE_FADEMETHOD ] ), TRUE );
        }

        if( history[FADE_METHOD] != info->status_tokens[FADE_METHOD] ) 
        {
            switch(info->status_tokens[FADE_METHOD]) {
                case -1:
                        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_FX_MNONE]), TRUE ); break;
                case 1: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_FX_M1]), TRUE ); break;
                case 2: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_FX_M2]), TRUE ); break;
                case 3: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_FX_M3]), TRUE ); break;
                case 4: gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_FX_M4]), TRUE ); break;
                default:
                        break;
            }
        }

        if(update)
        {
            speed = info->status_tokens[SAMPLE_SPEED];
            if(speed < 0 ) info->play_direction = -1; else info->play_direction = 1;

            gint len = info->status_tokens[SAMPLE_END] - info->status_tokens[SAMPLE_START];

            int speed = info->status_tokens[SAMPLE_SPEED];
            if(speed < 0 ) info->play_direction = -1; else info->play_direction = 1;
            if(speed < 0 ) speed *= -1;


            update_spin_range2( widget_cache[ WIDGET_SPIN_SAMPLESPEED ], -1 * len, len, speed );

            gtk_spin_button_set_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_SPIN_SAMPLESTART]), (gdouble) info->status_tokens[SAMPLE_START] );
            gtk_spin_button_set_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_SPIN_SAMPLEEND]), (gdouble) info->status_tokens[SAMPLE_END]);

            timeline_set_length( info->tl,
                (gdouble) info->status_tokens[SAMPLE_END], 
                info->status_tokens[FRAME_NUM] );

         //   update_spin_range( "spin_text_start", 0, n_frames ,0);
         //   update_spin_range( "spin_text_end", 0, n_frames,n_frames );

        }
    }

    if( pm == MODE_SAMPLE|| pm == MODE_STREAM )
    if( history[CHAIN_FADE] != info->status_tokens[CHAIN_FADE] )
    {
        double val = (double) info->status_tokens[CHAIN_FADE];
        update_slider_value( "manualopacity", val,0 );
    }

}


static void on_vims_messenger (void)
{
    if( !gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_VIMS_MESSENGER_PLAY] )) )
        return;

    GtkTextIter start, end;
    GtkTextBuffer* buffer;
    gchar *str = NULL;
    static int wait = 0;

    GtkTextView *t = GTK_TEXT_VIEW(GTK_WIDGET(glade_xml_get_widget_
                                 (info->main_window,"vims_messenger_textview")));
    buffer = gtk_text_view_get_buffer(t);

    if(info->vims_line >= gtk_text_buffer_get_line_count(buffer))
    {
        info->vims_line = 0;
    }

    if(wait)
    {
        wait--;
    }
    else
    {
        gtk_text_buffer_get_iter_at_line(buffer, &start, info->vims_line);
        end = start;

        gtk_text_iter_forward_sentence_end(&end);
        str = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);

	    if(strlen(str) <= 0) {
		    vj_msg(VEEJAY_MSG_INFO, "Nothing to do at line %d", info->vims_line);
		    info->vims_line++;
		    return;
	    }

        if(str[0] == '+')
        {
            str[0] = ' ';
            g_strstrip(str);
            wait = atoi(str);
		    vj_msg(VEEJAY_MSG_INFO, "Next VIMS message in %d frames", wait);
        }
        else
        {
		    msg_vims( str );
            vj_msg(VEEJAY_MSG_INFO, "Sent VIMS message '%s' (line %d)",str, info->vims_line );
        }
        info->vims_line++;    
    }
    
}

static int total_frames_ = 0;

int get_total_frames()
{
    return total_frames_;
}
/*
static char *bugbuffer_ = NULL;
static int bugoffset_ = 0;

gboolean capture_data   (GIOChannel *source, GIOCondition condition, gpointer data )
{
    int fd = g_io_channel_unix_get_fd( source );
    GIOStatus ret;
        GError *err = NULL;
        gchar *msg;
        gsize len;

        if (condition & G_IO_HUP)
                g_error ("Read end of pipe died!\n");

        ret = g_io_channel_read_line (source, &msg, &len, NULL, &err);
        if (ret == G_IO_STATUS_ERROR)
                g_error ("Error reading: %s\n", err->message);

    memcpy( bugbuffer_ + (sizeof(char) * bugoffset_) , msg , len );

    bugoffset_ += len;

        g_free (msg);
    return TRUE;
}
*/
void reportbug ()
{
    char l[3] = { 'e','n', '\0'};
    char *lang = getenv("LANG");
    char URL[1024];

    if(lang) {
        l[0] = lang[0];
        l[1] = lang[1];
    }
/*  char veejay_homedir[1024];
    char body[1024];
    char subj[100];
    gchar **argv = (gchar**) malloc ( sizeof(gchar*) * 5 );
    int i;
    argv[0] = malloc( sizeof(char) * 100 );
    memset( argv[0], 0, sizeof(char) * 100 );
    argv[2] = NULL;

//  snprintf(subj,sizeof(subj),"reloaded %s has a problem", VERSION);
    snprintf(veejay_homedir, sizeof(veejay_homedir),"%s/.veejay/", home );
    sprintf(argv[0], "%s/report_problem.sh" ,veejay_homedir);
    argv[1] = strdup( veejay_homedir );

    if( bugoffset_ > 0 )    {
        free(bugbuffer_);
        bugoffset_= 0;
        bugbuffer_ = NULL;
    }

//  GError      error = NULL;
    gint stdout_pipe = 0;
    gint pid =0;
    gboolean ret =  g_spawn_async_with_pipes(
                                                   NULL,
                    argv,
                    NULL,
                     G_SPAWN_LEAVE_DESCRIPTORS_OPEN & G_SPAWN_STDERR_TO_DEV_NULL,
                    NULL,
                    NULL,
                    &pid,
                    NULL,
                    &stdout_pipe,
                    NULL,
                    NULL );
    if( !ret ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Error executing bug report tool");
        return;
    }

    GIOChannel  *chan   = g_io_channel_unix_new( stdout_pipe );
    bugbuffer_ = (char*) malloc(sizeof(char) * 32000 );
    memset(bugbuffer_, 0, sizeof(char) * 32000);
    guint retb = g_io_add_watch( chan, G_IO_IN, capture_data, NULL );
*/
//  if( prompt_dialog("Report a problem", "" )
//       == GTK_RESPONSE_ACCEPT )
    snprintf(URL , sizeof(URL),
             "firefox \"http://groups.google.com/group/veejay-discussion/post?hl=%s\" &",l );

    puts(URL);

    if( system(URL) <= 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to open browser to veejay homepage");
    }
}

void donatenow()
{
    char URL[512];
    snprintf(URL , sizeof(URL),
             "firefox \"http://www.veejayhq.net/contributing\" &" );

    if( system(URL) <= 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to open browser to veejay homepage");
    }
}

static void reset_fxtree()
{
    int i;
    for(i = 0; i < 3; i ++ )
    {
        gtk_list_store_clear(fxlist_data.stores[i].list);
    }

}

static void reset_tree(const char *name)
{
    GtkWidget *tree_widget = glade_xml_get_widget_( info->main_window,name );
    GtkTreeModel *tree_model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree_widget) );


    if(GTK_IS_TREE_MODEL_SORT(tree_model)) {
        GtkTreeModel *child_model = gtk_tree_model_sort_get_model( GTK_TREE_MODEL_SORT(tree_model) );

        gtk_list_store_clear( GTK_LIST_STORE( child_model ) );
    }
    else {
        if( GTK_IS_LIST_STORE(tree_model) ) {
/*!
 * invalidate selection callback to prevent undesirable calls, clear the list
 * and finally restore selection callback.
 */
            GtkTreeSelection *selection;
            GtkTreeSelectionFunc selection_func;
            selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree_widget));
            selection_func = gtk_tree_selection_get_select_function(selection);
            gtk_tree_selection_set_select_function(selection, NULL, NULL, NULL);
            gtk_list_store_clear(GTK_LIST_STORE(tree_model));
            gtk_tree_selection_set_select_function(selection, selection_func, NULL, NULL);
        }
    }
}

void    select_chain_entry(int entry)
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");
    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));

    GtkTreePath *path = gtk_tree_path_new_from_indices( entry, -1 );
    gtk_tree_selection_select_path(sel, path);
    gtk_tree_path_free(path);
}
// load effect controls

gboolean view_entry_selection_func (GtkTreeSelection *selection,
                                    GtkTreeModel     *model,
                                    GtkTreePath      *path,
                                    gboolean          path_currently_selected,
                                    gpointer          userdata)
{
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        gint name = 0;

        gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);
        if (!path_currently_selected && name != info->uc.selected_chain_entry)
        {
            multi_vims( VIMS_CHAIN_SET_ENTRY, "%d", name );
            int sl = info->status_lock;
            info->status_lock = 1;
            if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_FX_MNONE]))) {
                multi_vims( VIMS_CHAIN_FADE_ENTRY,"%d %d",0, name );
            } else {
                multi_vims( VIMS_CHAIN_FADE_ENTRY,"%d %d",0,-1);
            }
            info->status_lock = sl;
 
            update_label_i( "label_fxentry", name, 0 );
            vj_midi_learning_vims_msg( info->midi, NULL, VIMS_CHAIN_SET_ENTRY,name );

            if( get_nums("button_fx_entry") != name ) {
                info->status_lock = 1;
                update_spin_value( "button_fx_entry", name   ); //FIXME
                info->uc.reload_hint[HINT_KF] = 1;
                info->uc.reload_hint[HINT_ENTRY] = 1;
                info->status_lock = 0;
            }

            return TRUE; /*the state of the node may be toggled*/
        }
        return TRUE;
    }

    return FALSE; /* the state of the node should be left unchanged */
}

gboolean cali_sources_selection_func (GtkTreeSelection *selection,
                                      GtkTreeModel     *model,
                                      GtkTreePath      *path,
                                      gboolean          path_currently_selected,
                                      gpointer          userdata)
{
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        gchar *name = NULL;

        if( info->uc.cali_stage != 0 )
        {
            veejay_msg(VEEJAY_MSG_ERROR, "%d", info->uc.cali_stage);
            return TRUE;
        }

        gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);

        if (!path_currently_selected)
        {
            gint id = 0;
            sscanf(name+1, "[ %d]", &id);
            if(name[0] != 'S')
            {
                cali_stream_id = id;
                update_label_str("current_step_label","Please take an image with the cap on the lens.");
                GtkWidget *nb = glade_xml_get_widget_(info->main_window, "cali_notebook");
                gtk_notebook_next_page( GTK_NOTEBOOK(nb));
            }
            if(name) g_free(name);
        }
    }
    return TRUE; /* allow selection state to change */
}

gboolean view_sources_selection_func (GtkTreeSelection *selection,
                                      GtkTreeModel     *model,
                                      GtkTreePath      *path,
                                      gboolean          path_currently_selected,
                                      gpointer          userdata)
{
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        gchar *name = NULL;

        gtk_tree_model_get(model, &iter, FXC_ID, &name, -1);

        if (!path_currently_selected)
        {
            gint id = 0;
            sscanf(name+1, "[ %d]", &id);
            if(name[0] == 'S')
            {
                info->uc.selected_mix_sample_id = id;
                info->uc.selected_mix_stream_id = 0;
            }
            else
            {
                info->uc.selected_mix_sample_id = 0;
                info->uc.selected_mix_stream_id = id;
            }
        }

        if(name) g_free(name);
    }

    return TRUE; /* allow selection state to change */
}

static void cell_data_func_dev (GtkTreeViewColumn *col,
                                GtkCellRenderer   *cell,
                                GtkTreeModel      *model,
                                GtkTreeIter       *iter,
                                gpointer           data)
{
    gchar   buf[32];
    GValue  val = {0, };
    gtk_tree_model_get_value(model, iter, V4L_SPINBOX, &val);
    g_snprintf(buf, sizeof(buf), "%.0f",g_value_get_float(&val));
    g_object_set(cell, "text", buf, NULL);
}

static void on_dev_edited (GtkCellRendererText *celltext,
                           const gchar         *string_path,
                           const gchar         *new_text,
                           gpointer             data)
{
    GtkTreeModel *model = GTK_TREE_MODEL(data);
    GtkTreeIter   iter;
    gfloat        oldval = 0.0;
    gfloat        newval = 0.0;

    gtk_tree_model_get_iter_from_string(model, &iter, string_path);

    gtk_tree_model_get(model, &iter, V4L_SPINBOX, &oldval, -1);
    if (sscanf(new_text, "%f", &newval) != 1)
        g_warning("in %s: problem converting string '%s' into float.\n", __FUNCTION__, new_text);

    gtk_list_store_set(GTK_LIST_STORE(model), &iter, V4L_SPINBOX, newval, -1);
}


static void setup_tree_spin_column( const char *tree_name, int type, const char *title)
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;
    renderer = gui_cell_renderer_spin_new(0.0, 3.0 , 1.0, 1.0, 1.0, 1.0, 0.0);
    column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, title );
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_set_cell_data_func( column, renderer,
            cell_data_func_dev, NULL,NULL );
    gtk_tree_view_append_column( GTK_TREE_VIEW(tree), column);
    g_object_set(renderer, "editable", TRUE, NULL);

    GtkTreeModel *model =  gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
    g_signal_connect(renderer, "edited", G_CALLBACK(on_dev_edited), model );

}

static void setup_tree_text_column( const char *tree_name, int type, const char *title,int len )
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes( title, renderer, "text", type, NULL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( tree ), column );

    if(len)
    {
        veejay_msg(VEEJAY_MSG_DEBUG, "Tree %s ,Title %s, width=%d", tree_name,title, len );
        gtk_tree_view_column_set_min_width( column, len);
    }
}

static void setup_tree_pixmap_column( const char *tree_name, int type, const char *title )
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, tree_name );
    GtkCellRenderer *renderer;
    GtkTreeViewColumn *column;

    renderer = gtk_cell_renderer_pixbuf_new();
        column = gtk_tree_view_column_new_with_attributes( title, renderer, "pixbuf", type, NULL );
    gtk_tree_view_append_column( GTK_TREE_VIEW( tree ), column );
}

void server_files_selection_func (GtkTreeView *treeview,
                                  GtkTreePath *path,
                                  GtkTreeViewColumn *col,
                                  gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    model = gtk_tree_view_get_model(treeview);

    if(gtk_tree_model_get_iter(model,&iter,path))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &iter, 0, &name, -1);

        multi_vims(VIMS_EDITLIST_ADD_SAMPLE, "0 %s" , name );
        vj_msg(VEEJAY_MSG_INFO, "Tried to open %s",name);
        g_free(name);
    }
}

void    generators_selection_func(GtkTreeView *treeview,
                                  GtkTreePath *path,
                                  GtkTreeViewColumn *col,
                                  gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    model = gtk_tree_view_get_model(treeview);

    if(gtk_tree_model_get_iter(model,&iter,path))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &iter, 0, &name, -1);

        multi_vims(VIMS_STREAM_NEW_GENERATOR, "0 %s" , name );
        vj_msg(VEEJAY_MSG_INFO, "Tried to open %s",name);
        g_free(name);

        gveejay_new_slot(MODE_STREAM);

        gtk_widget_hide( glade_xml_get_widget_(info->main_window, "generator_window") );
    }
}

static void setup_generators()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "generators");
    GtkListStore *store = gtk_list_store_new( 1,  G_TYPE_STRING );
    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    g_object_unref( G_OBJECT( store ));

    setup_tree_text_column( "generators", 0, "Filename",0 );

    g_signal_connect( tree, "row-activated", (GCallback) generators_selection_func, NULL);
}

static void setup_server_files(void)
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "server_files");
    GtkListStore *store = gtk_list_store_new( 1,  G_TYPE_STRING );
    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    g_object_unref( G_OBJECT( store ));

    setup_tree_text_column( "server_files", 0, "Filename",0 );

    g_signal_connect( tree, "row-activated", (GCallback) server_files_selection_func, NULL);
}

static void load_v4l_info()
{
    int values[21];
    int len = 0;

    veejay_memset(values,-1,sizeof(values));

    multi_vims( VIMS_STREAM_GET_V4L, "%d", (info->selected_slot == NULL ? 0 : info->selected_slot->sample_id ));
    gchar *answer = recv_vims(3, &len);
    if(len > 0 && answer )
    {
        int res = sscanf(answer, "%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d%05d",
                         &values[0],&values[1],&values[2],&values[3],&values[4],&values[5],
                         &values[6],&values[7],&values[8],&values[9],&values[10],&values[11],
                         &values[12],&values[13],&values[14],&values[15],&values[16],&values[17],&values[18],&values[19],&values[20]);
        if(res < 21 )
        {
            free(answer);
            return;
        }

        int i;
        int n = CAPT_CARD_SLIDERS;
        for(i = 0; i < n; i ++ )
        {
            if( values[i] < 0 ) {
                hide_widget( capt_card_set[i].name );
                hide_widget( capt_label_set[i].name );
            }
            else {
                show_widget( capt_card_set[i].name );
                show_widget( capt_label_set[i].name);
                update_slider_gvalue( capt_card_set[i].name, (gdouble)values[i]/65535.0 );
            }
        }
        n += CAPT_CARD_BOOLS;
        for( ; i < n; i ++ ) {
            if( values[i] < 0 ) {
                hide_widget( capt_card_set[i].name );
                hide_widget( capt_label_set[i].name );
            }
            else {
                show_widget( capt_card_set[i].name );
                show_widget( capt_label_set[i].name);
                set_toggle_button( capt_card_set[i].name, values[i] );
            }
        }
        free(answer);
    }
}

static gint load_parameter_info()
{
    int *p = &(info->uc.entry_tokens[0]);
    int len = 0;
    int i = 0;

    multi_vims( VIMS_CHAIN_GET_ENTRY, "%d %d", 0, info->uc.selected_chain_entry );

    gchar *answer = recv_vims(3,&len);

    int checksum = data_checksum(answer,len);
    if( info->uc.reload_hint_checksums[HINT_ENTRY] == checksum ) {
        if(answer) free(answer);
        return -1;
    }
 
    info->uc.reload_hint_checksums[HINT_ENTRY] = checksum;

    veejay_memset( p, 0, sizeof(info->uc.entry_tokens));

    if(len <= 0 || answer == NULL )
    {
        if(answer) free(answer);
        if(info->uc.selected_rgbkey )
            gtk_widget_set_sensitive_( widget_cache[WIDGET_RGBKEY], FALSE );
        return 0;
    }

    char *ptr;
    char *token = strtok_r( answer," ", &ptr );
    if(!token) {
        veejay_msg(VEEJAY_MSG_ERROR,"Invalid reply from %d", VIMS_CHAIN_GET_ENTRY );
        free(answer);
        return 0;
    }
    p[i] = atoi(token);
    while( (token = strtok_r( NULL, " ", &ptr ) ) != NULL )
    {
        i++;
        p[i] = atoi( token );
    }

    info->uc.selected_rgbkey = _effect_get_rgb( p[0] );
    if(info->uc.selected_rgbkey)
    {
        gtk_widget_set_sensitive_( widget_cache[WIDGET_RGBKEY], TRUE );
        update_rgbkey();
    }
    else
    {
        gtk_widget_set_sensitive_( widget_cache[WIDGET_RGBKEY], FALSE );
        info->uc.selected_rgbkey = 0;
    }

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_CHAIN_TOGGLEENTRY])) != p[ENTRY_KF_STATUS]){
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_CHAIN_TOGGLEENTRY]), p[ENTRY_KF_STATUS]);
    }

    switch( p[ENTRY_KF_TYPE] )
    {
        case GTK3_CURVE_TYPE_SPLINE:
            if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPESPLINE]))) {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPESPLINE]), TRUE );
             }
             break;
        case GTK3_CURVE_TYPE_FREE:
             if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPEFREEHAND]))) {
                 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPEFREEHAND]), TRUE );
             }
             break;
        case GTK3_CURVE_TYPE_LINEAR:
            if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPELINEAR]))) {
                 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_TYPELINEAR]), TRUE );
            }
            break;
        default:
            break;
    }

    if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE])) != p[ENTRY_SUBRENDER_ENTRY]) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE]), p[ENTRY_SUBRENDER_ENTRY]);
    }

    free(answer);
    return 1;
}

static void load_generator_info()
{
    int args[GENERATOR_PARAMS+1];
    gint fxlen = 0;
    multi_vims( VIMS_GET_STREAM_ARGS,"%d",0 );
    gchar *fxtext = recv_vims(3,&fxlen);

    int checksum = data_checksum(fxtext,fxlen);
    if( info->uc.reload_hint_checksums[HINT_GENERATOR] == checksum ) {
        if(fxtext) free(fxtext);
        return;
    }
    info->uc.reload_hint_checksums[HINT_GENERATOR] = checksum;

    if(fxtext == NULL)
        return;

    veejay_memset(args,0,sizeof(args));
    generator_to_arr(fxtext, args);

    int np = _effect_get_np( args[0] );
    int i;

    for( i = 0; i < np ; i ++ )
    {
        if(!gtk_widget_is_sensitive( GTK_WIDGET(widget_cache[WIDGET_SLIDER_BOX_G0 + i] ))) {
            gtk_widget_set_sensitive( GTK_WIDGET(widget_cache[WIDGET_SLIDER_BOX_G0 + i] ), TRUE );
        }

        gchar *tt1 = _utf8str(_effect_get_param_description( args[0],i));
        set_tooltip( gen_names_[i].text, tt1 );

        gint min=0,max=0,value = 0;
        value = args[1 + i];

        if( _effect_get_minmax( args[0], &min,&max, i ))
        {
            update_slider_range( gen_names_[i].text,min,max, value, 0);
        }
        g_free(tt1);
    }

    for( i = np; i < GENERATOR_PARAMS; i ++ )
    {
        gint min = 0, max = 1, value = 0;
        update_slider_range( gen_names_[i].text, min,max, value, 0 );

        if(gtk_widget_is_sensitive( GTK_WIDGET(widget_cache[WIDGET_SLIDER_BOX_G0 + i] ))) {
            gtk_widget_set_sensitive( GTK_WIDGET(widget_cache[WIDGET_SLIDER_BOX_G0 + i] ), FALSE );
        }

        set_tooltip( gen_names_[i].text, "" );
    }

    free(fxtext);
}

/******************************************************
 *
 *                    EFFECT CHAIN
 *
 ******************************************************/

/******************************************************
 * setup_effectchain_info()
 *   setup tree effect chain model and selection mode
 *
 ******************************************************/
static void setup_effectchain_info( void )
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");
    GtkListStore *store = gtk_list_store_new( FXC_N_COLS, G_TYPE_INT, G_TYPE_STRING, GDK_TYPE_PIXBUF,GDK_TYPE_PIXBUF,G_TYPE_STRING, GDK_TYPE_PIXBUF, G_TYPE_INT);
    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    g_object_unref( G_OBJECT( store ));

    setup_tree_text_column( "tree_chain", FXC_ID, "#",0 );
    setup_tree_text_column( "tree_chain", FXC_FXID, "Effect",0 ); //FIXME
    setup_tree_pixmap_column( "tree_chain", FXC_FXSTATUS, "Run");
    setup_tree_pixmap_column( "tree_chain", FXC_KF , "Anim" ); // TODO parameter interpolation on/off per entry
    setup_tree_text_column( "tree_chain", FXC_MIXING, "Channel",0);
    setup_tree_pixmap_column( "tree_chain", FXC_SUBRENDER, "Subrender");
    GtkTreeSelection *selection;

    // selection stuff
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
    gtk_tree_selection_set_select_function(selection, view_entry_selection_func, NULL, NULL);

    // signal stuff (button press)
    g_signal_connect(GTK_TREE_VIEW(tree), "button-press-event",
                     (GCallback) on_effectchain_button_pressed, NULL);
}


/******************************************************
 * load_effectchain_info()
 *   load effect chain from server (VIMS transmition)
 *   to the fx chain tree view
 *
 ******************************************************/
static void load_effectchain_info()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_chain");

    gint fxlen = 0;
    multi_vims( VIMS_CHAIN_LIST,"%d",0 );
    gchar *fxtext = recv_vims(3,&fxlen);

    int checksum = data_checksum(fxtext,fxlen);
    if( info->uc.reload_hint_checksums[HINT_CHAIN] == checksum ) {
        if(fxtext) free(fxtext);
        return;
    }
    info->uc.reload_hint_checksums[HINT_CHAIN] = checksum;

    GtkListStore *store;
    gchar toggle[4];
    guint arr[VIMS_CHAIN_LIST_ENTRY_VALUES];
    GtkTreeIter iter;
    gint offset=0;

    set_tooltip_by_widget (tree, tooltips[TOOLTIP_FXCHAINTREE].text);

    reset_tree( "tree_chain" );

    GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
    store = GTK_LIST_STORE(model);

    //update chain fx status
    gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_CHAIN_TOGGLECHAIN]), info->status_tokens[SAMPLE_FX] );
    //also for stream (index is equivalent)
    if(info->status_tokens[PLAY_MODE] == MODE_SAMPLE){
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CHECK_SAMPLEFX]), info->status_tokens[SAMPLE_FX] );
    } else {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CHECK_STREAMFX]), info->status_tokens[SAMPLE_FX] );
    }

    // no fx, clean list and return
    if(fxlen <= 0 )
    {
        int i;
        for( i = 0; i < SAMPLE_MAX_EFFECTS; i ++ )
        {
            gtk_list_store_append(store,&iter);
            gtk_list_store_set(store,&iter, FXC_ID, i ,-1);
            gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
        }
        return;
    }

    if(fxlen == 5 )
        offset = fxlen;

    gint last_index =0;

    while( offset < fxlen )
    {
        char line[VIMS_CHAIN_LIST_ENTRY_LENGTH+1];
        veejay_memset(arr,0,sizeof(arr));
        veejay_memset(line,0,sizeof(line));

        strncpy( line, fxtext + offset, VIMS_CHAIN_LIST_ENTRY_LENGTH );
        int n_tokens = sscanf( line, VIMS_CHAIN_LIST_ENTRY_FORMAT,
               &arr[0],&arr[1],&arr[2],&arr[3],&arr[4],&arr[5],&arr[6], &arr[7], &arr[8]);
        if( n_tokens != VIMS_CHAIN_LIST_ENTRY_VALUES ) {
            veejay_msg(0,"Error parsing FX chain response");
            break;
        }

        // clean list until next entry
        while( last_index < arr[0] )
        {
            gtk_list_store_append( store, &iter );
            gtk_list_store_set( store, &iter, FXC_ID, last_index,-1);
            last_index ++;
        }

        // time to fill current entry
        char *name = _effect_get_description( arr[1] );
        snprintf(toggle,sizeof(toggle),"%s",arr[3] == 1 ? "on" : "off" );

        if( last_index == arr[0])
        {
            gchar *utf8_name = _utf8str( name );
            char  tmp[128];
            if( _effect_get_mix( arr[1] ) ) {
                snprintf(tmp,sizeof(tmp),"%s %d", (arr[5] == 0 ? "Sample " : "T " ),
                    arr[6]);
            }
            else {
                snprintf(tmp,sizeof(tmp),"%s"," ");
            }
            gchar *mixing = _utf8str(tmp);

            gtk_list_store_append( store, &iter );
            int kf_status = arr[7];
            GdkPixbuf *toggle = update_pixmap_entry( arr[3] );
            GdkPixbuf *kf_togglepf = update_pixmap_entry( kf_status );
            GdkPixbuf *subrender_toggle = update_pixmap_entry(arr[8]);
            gtk_list_store_set( store, &iter,
                               FXC_ID, arr[0],
                               FXC_FXID, utf8_name,
                               FXC_FXSTATUS, toggle,
                               FXC_KF, kf_togglepf,
                               FXC_MIXING,mixing, 
                               FXC_SUBRENDER, subrender_toggle,
                               FXC_KF_STATUS, kf_status,
                                -1 );
            last_index ++;
            g_free(utf8_name);
            g_free(mixing);
            g_object_unref( toggle );
            g_object_unref( kf_togglepf );
            g_object_unref( subrender_toggle );
        }
        offset += VIMS_CHAIN_LIST_ENTRY_LENGTH;
    }

    // finally clean list end
    while( last_index < SAMPLE_MAX_EFFECTS )
    {
        gtk_list_store_append( store, &iter );
        gtk_list_store_set( store, &iter,
            FXC_ID, last_index , -1 );
        last_index ++;
    }
    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    free(fxtext);
}


/******************************************************
 *
 *                    EFFECTS LISTS
 *
 ******************************************************/

enum
{
//  FX_ID = 0,
    FX_STRING = 0,
    FX_NUM,
};

gboolean view_fx_selection_func (GtkTreeSelection *selection,
                                 GtkTreeModel     *model,
                                 GtkTreePath      *path,
                                 gboolean          path_currently_selected,
                                 gpointer          userdata)
{
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &iter, FX_STRING, &name, -1);

        if (!path_currently_selected)
        {
            int value = 0;
            vevo_property_get( fx_list_, name, 0, &value );
            if(value)
            {
                info->uc.selected_effect_id = value;
            }
        }
    g_free(name);
    }

    return TRUE; /* allow selection state to change */
}

static guint effectlist_add_mask = 0;
static const guint FXLIST_ADD_DISABLED = 1 << 1;
static const guint FXLIST_ADD_TO_SELECTED = 1 << 2;

gboolean on_effectlist_row_key_pressed (GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    if(event->type & GDK_KEY_PRESS){
        switch(event->keyval){
            case GDK_KEY_Shift_L:
                effectlist_add_mask |= FXLIST_ADD_DISABLED;
                break;
            case GDK_KEY_Control_L:
                effectlist_add_mask |= FXLIST_ADD_TO_SELECTED;
                break;
        }
    }
    return FALSE;
}

gboolean on_effectlist_row_key_released (GtkWidget *widget, GdkEventKey  *event, gpointer   user_data)
{
    if(event->type & GDK_KEY_RELEASE){
        switch(event->keyval){
            case GDK_KEY_Shift_L:
                effectlist_add_mask &= !(FXLIST_ADD_DISABLED);
                break;
            case GDK_KEY_Control_L:
                effectlist_add_mask &= !(FXLIST_ADD_TO_SELECTED);
                break;
        }
    }
    return FALSE;
}

void on_effectlist_row_activated(GtkTreeView *treeview,
                                 GtkTreePath *path,
                                 GtkTreeViewColumn *col,
                                 gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    model = gtk_tree_view_get_model(treeview);

    if(gtk_tree_model_get_iter(model,&iter,path)) {
        gint gid =0;
        gchar *name = NULL;
        gtk_tree_model_get(model,&iter, FX_STRING, &name, -1);
        if(vevo_property_get( fx_list_, name, 0, &gid ) == 0 ) {
            guint slot = 0;
            
            if((effectlist_add_mask & FXLIST_ADD_TO_SELECTED) && info->selection_slot)
                slot = info->selection_slot->sample_id;

            multi_vims(VIMS_CHAIN_ENTRY_SET_EFFECT, "%d %d %d %d",
                slot, info->uc.selected_chain_entry,gid, !(effectlist_add_mask & FXLIST_ADD_DISABLED) );
            
            char trip[100];
            snprintf(trip,sizeof(trip), "%03d:%d %d %d %d;", VIMS_CHAIN_ENTRY_SET_EFFECT,slot,info->uc.selected_chain_entry, gid, !(effectlist_add_mask & FXLIST_ADD_DISABLED) );
            vj_midi_learning_vims( info->midi, NULL, trip, 0 );
            
            info->uc.reload_hint[HINT_CHAIN] = 1;
            info->uc.reload_hint[HINT_ENTRY] = 1;
            info->uc.reload_hint[HINT_KF] = 1;

        }
        g_free(name);
    }
}

gint sort_iter_compare_func( GtkTreeModel *model,
                            GtkTreeIter *a,
                            GtkTreeIter *b,
                            gpointer userdata)
{
    gint sortcol = GPOINTER_TO_INT(userdata);
    gint ret = 0;

    if(sortcol == FX_STRING)
    {
        gchar *name1=NULL;
        gchar *name2=NULL;
        gtk_tree_model_get(model,a, FX_STRING, &name1, -1 );
        gtk_tree_model_get(model,b, FX_STRING, &name2, -1 );
        if( name1 == NULL || name2 == NULL )
        {
            if( name1==NULL && name2==NULL)
            {
                return 0;
            }
            ret = (name1 == NULL) ? -1 : 1;
        }
        else
        {
            ret = g_utf8_collate(name1,name2);
        }
        if(name1) g_free(name1);
        if(name2) g_free(name2);
    }
    return ret;
}

gint sort_vims_func(GtkTreeModel *model,
                    GtkTreeIter *a,
                    GtkTreeIter *b,
                    gpointer userdata)
{
    gint sortcol = GPOINTER_TO_INT(userdata);
    gint ret = 0;

    if(sortcol == VIMS_ID)
    {
        gchar *name1 = NULL;
        gchar *name2 = NULL;

        gtk_tree_model_get(model,a, VIMS_ID, &name1, -1 );
        gtk_tree_model_get(model,b, VIMS_ID, &name2, -1 );
        if( name1 == NULL || name2 == NULL )
        {
            if( name1==NULL && name2== NULL)
            {
                return 0;
            }
            ret = (name1==NULL) ? -1 : 1;
        }
        else
        {
            ret = g_utf8_collate(name1,name2);
        }
        if(name1) g_free(name1);
        if(name2) g_free(name2);
    }
    return ret;
}


//EffectListData* get_effectlistdata

static gboolean effect_row_visible (GtkTreeModel *model, GtkTreeIter *iter, gpointer user_data)
{
    effectlist_data *fxlistdata = (effectlist_data*) user_data;
    gboolean value=TRUE;
    if(fxlistdata != NULL && fxlistdata->filter_string != NULL) {
        if(strlen(fxlistdata->filter_string)) {
            value=FALSE;
            gchar *idstr = NULL;
            gtk_tree_model_get(model, iter, SL_ID, &idstr, -1);
            if((idstr != NULL) && strcasestr(idstr, fxlistdata->filter_string) != NULL) {
                value = TRUE;
            }
            g_free (idstr);
        }
    }
    return value;
}


/******************************************************
 * setup_effectlist_info()
 *   prepare the views of effects lists
 *
 * Three treeview : fxlist mixlist, alphalist
 *
 ******************************************************/
void setup_effectlist_info()
{
    int i;
    GtkWidget *trees[3];
    trees[0] = glade_xml_get_widget_( info->main_window, "tree_effectlist");
    trees[1] = glade_xml_get_widget_( info->main_window, "tree_effectmixlist");
    trees[2] = glade_xml_get_widget_( info->main_window, "tree_alphalist" );

    set_tooltip_by_widget (trees[0], tooltips[TOOLTIP_FXSELECT].text);
    set_tooltip_by_widget (trees[1], tooltips[TOOLTIP_FXSELECT].text);
    set_tooltip_by_widget (trees[2], tooltips[TOOLTIP_FXSELECT].text);

    fx_list_ = (vevo_port_t*) vpn( 200 );

    for(i = 0; i < 3; i ++ )
    {
        fxlist_data.stores[i].list = gtk_list_store_new( 1, G_TYPE_STRING );

        fxlist_data.stores[i].filtered = GTK_TREE_MODEL_FILTER (gtk_tree_model_filter_new (GTK_TREE_MODEL (fxlist_data.stores[i].list), NULL));
        fxlist_data.stores[i].sorted = GTK_TREE_MODEL_SORT (gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (fxlist_data.stores[i].filtered)));

        fxlist_data.filter_string = NULL;
        gtk_tree_model_filter_set_visible_func (fxlist_data.stores[i].filtered,
                                                effect_row_visible,
                                                &fxlist_data, NULL);

        GtkTreeSortable *sortable = GTK_TREE_SORTABLE(fxlist_data.stores[i].list);
        gtk_tree_sortable_set_sort_func(sortable, FX_STRING,
                                        sort_iter_compare_func,
                                        GINT_TO_POINTER(FX_STRING),NULL);

        gtk_tree_sortable_set_sort_column_id(sortable, FX_STRING, GTK_SORT_ASCENDING);
        gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(trees[i]), FALSE );

        gtk_tree_view_set_model( GTK_TREE_VIEW(trees[i]),GTK_TREE_MODEL( fxlist_data.stores[i].sorted));
        g_object_unref( G_OBJECT( fxlist_data.stores[i].list ));
    }

    setup_tree_text_column( "tree_effectlist", FX_STRING, "Effect",0 );
    setup_tree_text_column( "tree_effectmixlist", FX_STRING, "Effect",0 );
    setup_tree_text_column( "tree_alphalist", FX_STRING, "Alpha",0);

    for(i = 0; i < 3;  i ++ )
    {
        g_signal_connect( trees[i],"row-activated", (GCallback) on_effectlist_row_activated, NULL );
        GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(trees[i]));
        gtk_tree_selection_set_select_function(selection, view_fx_selection_func, NULL, NULL);
        gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

        g_signal_connect( G_OBJECT(trees[i]), "key_press_event", G_CALLBACK( on_effectlist_row_key_pressed ), NULL );
        g_signal_connect( G_OBJECT(trees[i]), "key-release-event", G_CALLBACK( on_effectlist_row_key_released ), NULL );
    }

    GtkWidget *entry_filterfx = glade_xml_get_widget_( info->main_window, "filter_effects");
    set_tooltip_by_widget (entry_filterfx, tooltips[TOOLTIP_FXFILTER].text);
    g_signal_connect(G_OBJECT(entry_filterfx),
                     "changed",
                     G_CALLBACK( on_filter_effects_changed ),
                     &fxlist_data );

}

void set_feedback_status()
{
	int len = 0;
	single_vims(VIMS_GET_FEEDBACK);
	gchar *answer = recv_vims(3,&len);
	if(answer == NULL)
		return;

	int status = atoi(answer);

	set_toggle_button("feedbackbutton", status);

	g_free(answer);
}

/******************************************************
 * load_effectlist_info()
 *   load the effects information from the server
 *   (VIMS transmission) to the treeviews
 *
 ******************************************************/
void load_effectlist_info()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_effectlist");
    GtkWidget *tree2 = glade_xml_get_widget_( info->main_window, "tree_effectmixlist");
    GtkWidget *tree3 = glade_xml_get_widget_( info->main_window, "tree_alphalist");
    GtkListStore *store,*store2,*store3;
    char line[4096];

    GtkTreeIter iter;
    gint i,offset=0;
    gint fxlen = 0;
    single_vims( VIMS_EFFECT_LIST );
    gchar *fxtext = recv_vims(6,&fxlen);

    _effect_reset();

    reset_fxtree();

    store = fxlist_data.stores[0].list;
    store2 = fxlist_data.stores[1].list;
    store3 = fxlist_data.stores[2].list;

    int ec_idx = 0;
    int hi_id = 0;
    while( offset < fxlen )
    {
        char tmp_len[4];
        veejay_memset(tmp_len,0,sizeof(tmp_len));
        strncpy(tmp_len, fxtext + offset, 3 );
        int len = atoi(tmp_len);
        offset += 3;
        if(len > 0)
        {
            effect_constr *ec;
            veejay_memset( line,0,sizeof(line));
            strncpy( line, fxtext + offset, len );

            ec = _effect_new(line);
            if( ec  ) {
                info->effect_info[ec->id] = ec;
                ec_idx ++;
                if( hi_id < ec->id )
                    hi_id = ec->id;
            }
        }
        offset += len;
    }

    for( i = 0; i <= hi_id; i ++)
    {
        effect_constr *ec = info->effect_info[i];
        if(ec == NULL)
            continue;
        if( ec->is_gen )
            continue;
        gchar *name = _utf8str( ec->description );

        if( name != NULL)
        {
            // tree_alphalist
            if( strncasecmp( "alpha:" , ec->description, 6 ) == 0 )
            {
                gtk_list_store_append( store3, &iter );
                int len = strlen( ec->description );
                char *newName = vj_calloc( len );
                veejay_memcpy(newName,ec->description+6, len-6 );
                gtk_list_store_set( store3,&iter, FX_STRING, newName, -1 );
                vevo_property_set( fx_list_, newName, VEVO_ATOM_TYPE_INT,1,&(ec->id));
                free(newName);
            }
            else
            {
                // tree_effectmixlist
                if( ec->is_video )
                {
                    gtk_list_store_append( store2, &iter );
                    gtk_list_store_set( store2, &iter, FX_STRING, name, -1 );
                    vevo_property_set( fx_list_, name, VEVO_ATOM_TYPE_INT, 1, &(ec->id));
                }
                else
                {
                    // tree_effectlist
                    gtk_list_store_append( store, &iter );
                    gtk_list_store_set( store, &iter, FX_STRING, name, -1 );
                    vevo_property_set( fx_list_, name, VEVO_ATOM_TYPE_INT, 1, &(ec->id));
                }
            }
        }
        g_free(name);
    }

    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(fxlist_data.stores[0].sorted));
    gtk_tree_view_set_model( GTK_TREE_VIEW(tree2), GTK_TREE_MODEL(fxlist_data.stores[1].sorted));
    gtk_tree_view_set_model( GTK_TREE_VIEW(tree3), GTK_TREE_MODEL(fxlist_data.stores[2].sorted));
    free(fxtext);

    veejay_msg(VEEJAY_MSG_DEBUG, "Loaded %d effects (highest ID is %d)", ec_idx, hi_id );
}

void on_effectlist_sources_row_activated(GtkTreeView *treeview,
                                         GtkTreePath *path,
                                         GtkTreeViewColumn *col,
                                         gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    model = gtk_tree_view_get_model(treeview);


    if(gtk_tree_model_get_iter(model,&iter,path))
    {
        gchar *idstr = NULL;
        gtk_tree_model_get(model,&iter, SL_ID, &idstr, -1);
        gint id = 0;
        if( sscanf( idstr+1, "[ %d]", &id ) )
        {
            // set source / channel
            multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
            "%d %d %d %d",
            0,
            info->uc.selected_chain_entry,
            ( idstr[0] == 'T' ? 1 : 0 ),
            id );
            vj_msg(VEEJAY_MSG_INFO, "Set source channel to %d, %d", info->uc.selected_chain_entry,id );

            char trip[100];
            snprintf(trip, sizeof(trip), "%03d:%d %d %d %d",VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
                0,
                info->uc.selected_chain_entry,
                ( idstr[0] == 'T' ? 1 : 0 ),
                id );
            vj_midi_learning_vims( info->midi, NULL, trip, 0 );
        }
        if(idstr) g_free(idstr);
    }
}

/******************************************************
 *
 *                    SAMPLES LIST
 *
 ******************************************************/

/* Return a bank page and slot number to place sample in */
int verify_bank_capacity(int *bank_page_, int *slot_, int sample_id, int sample_type )
{
    int poke_slot = 0;
    int bank_page = find_bank_by_sample( sample_id, sample_type, &poke_slot );

    if(bank_page == -1) {
        veejay_msg(VEEJAY_MSG_ERROR, "No slot found for (%d,%d)",sample_id,sample_type);
        return -1;
    }

    if( !bank_exists(bank_page, poke_slot))
        add_bank( bank_page );

    *bank_page_ = bank_page;
    *slot_      = poke_slot;

    return 1;
}

void setup_samplelist_info()
{
    effect_sources_tree = glade_xml_get_widget_( info->main_window, "tree_sources");
    effect_sources_store = gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING );

    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW(effect_sources_tree), FALSE );

    gtk_tree_view_set_model( GTK_TREE_VIEW(effect_sources_tree), GTK_TREE_MODEL(effect_sources_store));
    g_object_unref( G_OBJECT( effect_sources_store ));
    effect_sources_model = gtk_tree_view_get_model( GTK_TREE_VIEW(effect_sources_tree ));
    effect_sources_store = GTK_LIST_STORE(effect_sources_model);

    setup_tree_text_column( "tree_sources", SL_ID, "Id",0 );
    setup_tree_text_column( "tree_sources", SL_TIMECODE, "Length" ,0);

    GtkTreeSelection *selection;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(effect_sources_tree));
    gtk_tree_selection_set_select_function(selection, view_sources_selection_func, NULL, NULL);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

    g_signal_connect( effect_sources_tree, "row-activated", (GCallback) on_effectlist_sources_row_activated, (gpointer*)"tree_sources");

    cali_sourcetree = glade_xml_get_widget_(info->main_window, "cali_sourcetree");
    cali_sourcestore= gtk_list_store_new( 3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING );

    gtk_tree_view_set_headers_visible( GTK_TREE_VIEW( cali_sourcetree), FALSE );
    gtk_tree_view_set_model( GTK_TREE_VIEW(cali_sourcetree), GTK_TREE_MODEL(cali_sourcestore));
    g_object_unref( G_OBJECT(cali_sourcestore));

    cali_sourcemodel = gtk_tree_view_get_model( GTK_TREE_VIEW(cali_sourcetree ));
    cali_sourcestore = GTK_LIST_STORE(cali_sourcemodel);

    setup_tree_text_column( "cali_sourcetree", SL_ID, "Id",0 );
    setup_tree_text_column( "cali_sourcetree", SL_TIMECODE, "Length" ,0);

    GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(cali_sourcetree));
    gtk_tree_selection_set_select_function(sel, cali_sources_selection_func, NULL, NULL);
    gtk_tree_selection_set_mode(sel, GTK_SELECTION_SINGLE);

//  g_signal_connect( cali_sourcetree, "row-activated", (GCallback) on_effectlist_sources_row_activated, (gpointer*)"tree_sources");
}

static uint8_t *ref_trashcan[3] = { NULL,NULL,NULL };
static GdkPixbuf *pix_trashcan[3] = { NULL,NULL,NULL };

void reset_cali_images( int type, char *wid_name )
{
    GtkWidget *dstImage = glade_xml_get_widget_( info->main_window, wid_name );

    if( pix_trashcan[type] != NULL ) {
        g_object_unref( pix_trashcan[type] );
        pix_trashcan[type] = NULL;
    }
    if( ref_trashcan[type] != NULL  ) {
        free( ref_trashcan[type] );
        ref_trashcan[type] = NULL;
    }
    gtk_image_clear( GTK_IMAGE(dstImage) );
}

int get_and_draw_frame(int type, char *wid_name)
{
    GtkWidget *dstImage = glade_xml_get_widget_( info->main_window, wid_name );
    if(dstImage == 0 ) {
        veejay_msg(VEEJAY_MSG_ERROR, "No widget '%s'",wid_name);
        return 0;
    }

    multi_vims( VIMS_CALI_IMAGE, "%d %d", cali_stream_id,type);

    int bw = 0;
    gchar *buf = recv_vims( 3, &bw );

    if( bw <= 0 )
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Unable to get calibration image.");
        return 0;
    }

    int len = 0;
    int uvlen = 0;
    int w = 0;
    int h = 0;
    int tlen = 0;
    if( sscanf(buf,"%08d%06d%06d%06d%06d",&tlen, &len, &uvlen,&w,&h) != 5 ) {
        free(buf);
        veejay_msg(VEEJAY_MSG_ERROR,"Error reading calibration data header" );
        return 0;
    }

    uint8_t *out = (uint8_t*) vj_malloc(sizeof(uint8_t) * (w*h*3));
    uint8_t *srcbuf = (uint8_t*) vj_malloc(sizeof(uint8_t) * len );

    int res = vj_client_read(info->client, V_CMD, srcbuf, tlen );
    if( res <= 0 ) {
        free(out);
        free(srcbuf);
        free(buf);
        veejay_msg(VEEJAY_MSG_ERROR, "Error while receiving calibration image.");
        return 0;
    }

    VJFrame *src = yuv_yuv_template(srcbuf,
                                    srcbuf,
                                    srcbuf,
                                    w,
                                    h,
                                    PIX_FMT_GRAY8 );

    VJFrame *dst = yuv_rgb_template( out, w,h,PIX_FMT_BGR24 );

    yuv_convert_any_ac( src,dst, src->format, dst->format );

    GdkPixbuf *pix = gdk_pixbuf_new_from_data(out,
                                              GDK_COLORSPACE_RGB,
                                              FALSE,
                                              8,
                                              w,
                                              h,
                                              w*3,
                                              NULL,
                                              NULL );

    if( ref_trashcan[type] != NULL )
    {
        free(ref_trashcan[type]);
        ref_trashcan[type]=NULL;
    }
    if( pix_trashcan[type] != NULL )
    {
        g_object_unref( pix_trashcan[type] );
        pix_trashcan[type] = NULL;
    }

    gtk_image_set_from_pixbuf_( GTK_IMAGE( dstImage ), pix );

    //  gdk_pixbuf_unref( pix );

    free(src);
    free(dst);
    free(buf);
//  free(out);
    free(srcbuf);

    ref_trashcan[type] = out;
    pix_trashcan[type] = pix;

    return 1;
}

static void select_slot( int pm )
{
    if( pm == MODE_SAMPLE || pm == MODE_STREAM  )
    {
        int b = 0; int p = 0;
        if(info->status_tokens[CURRENT_ID] > 0)
        {
            if(verify_bank_capacity( &b, &p, info->status_tokens[CURRENT_ID], info->status_tokens[STREAM_TYPE] ))
            {
                set_activation_of_slot_in_samplebank(FALSE);
                info->selected_slot = info->sample_banks[b]->slot[p];
                info->selected_gui_slot = info->sample_banks[b]->gui_slot[p];
                set_activation_of_slot_in_samplebank(TRUE);
            }
        }
    }
    else
    {
        set_activation_of_slot_in_samplebank(FALSE);
        info->selected_slot = NULL;
        info->selected_gui_slot = NULL;
    }
}

static void load_sequence_list()
{
    single_vims( VIMS_SEQUENCE_LIST );
    gint len = 0;
    gchar *text = recv_vims( 6, &len );
    if( len <= 0 || text == NULL )
        return;

    int checksum = data_checksum(text,len);
    if( info->uc.reload_hint_checksums[HINT_SEQ_ACT] == checksum ) {
        if(text) free(text);
        return;
    }
    info->uc.reload_hint_checksums[HINT_SEQ_ACT] = checksum;


    int playing=0;
    int size =0;
    int active=0;

    sscanf( text, "%04d%04d%4d",&playing,&size,&active );
    int nlen = len - 12;
    int offset = 0;
    int id = 0;
    gchar *in = text + 12;
    while( offset < nlen )
    {
        int sample_id = 0;
        int type = 0;
        char seqtext[32];
        sscanf( in + offset, "%04d%02d", &sample_id, &type );
        offset += 6;
        if( sample_id > 0 )
        {
            sprintf(seqtext,"%c%d",( type == 0 ? 'S' : 'T' ), sample_id);
            gtk_label_set_text(
                GTK_LABEL(info->sequencer_view->gui_slot[id]->image),
                seqtext );
        }
        else
        {
            gtk_label_set_text(
                    GTK_LABEL(info->sequencer_view->gui_slot[id]->image),
                    NULL );
        }

        id ++;
    }
    free(text);
}

static void load_samplelist_info(gboolean with_reset_slotselection)
{
    char line[300];
    char source[255];
    char descr[255];
    gint offset=0;
    gint no_samples = 1;

    if( cali_onoff == 1 )
        reset_tree( "cali_sourcetree");

    if( with_reset_slotselection )
    {
        reset_samplebank();
        reset_tree( "tree_sources" );
    }

    int load_from = info->uc.expected_num_samples;
    if( load_from < 0 )
        load_from = 0;


    multi_vims( VIMS_SAMPLE_LIST,"%d", (with_reset_slotselection ? 0 : load_from) );
    gint fxlen = 0;
    gchar *fxtext = recv_vims(8,&fxlen);

    if(fxlen > 0 && fxtext != NULL)
    {
        no_samples = 0;
        while( offset < fxlen )
        {
            char tmp_len[8] = { 0 };
            strncpy(tmp_len, fxtext + offset, 3 );
            int len = atoi(tmp_len);
            offset += 3;
            if(len > 0)
            {
                veejay_memset( line,0,sizeof(line));
                veejay_memset( descr,0,sizeof(descr));
                strncpy( line, fxtext + offset, len );
                int values[4] = { 0,0,0,0 };
                sscanf( line, "%05d%09d%09d%03d",&values[0], &values[1], &values[2], &values[3]);
                strncpy( descr, line + 5 + 9 + 9 + 3 , values[3] );
                gchar *title = _utf8str( descr );
                char *timecode = format_selection_time( 0,(values[2]-values[1]) );
                int int_id = values[0];
                int poke_slot= 0; int bank_page = -1;
                verify_bank_capacity( &bank_page , &poke_slot, int_id, values[1]);
                if(bank_page >= 0 )
                {
                    if( info->sample_banks[bank_page]->slot[poke_slot]->sample_id <= 0 )
                    {
                        sample_slot_t *tmp_slot = create_temporary_slot(poke_slot,int_id,0, title,timecode );
                        add_sample_to_sample_banks(bank_page, tmp_slot );
                        add_sample_to_effect_sources_list( int_id,0, title, timecode);
                        free_slot(tmp_slot);
                        if( !disable_sample_image ) {
                            veejay_get_sample_image( int_id, 0, info->image_dimensions[0], info->image_dimensions[1] );
                        }
                    }
                    else
                    {
                        update_sample_slot_data( bank_page, poke_slot, int_id,0,title,timecode);
                    }
                }
                if( info->status_tokens[CURRENT_ID] == values[0] && info->status_tokens[PLAY_MODE] == 0 )
                    put_text( "entry_samplename", title );
                free(timecode);
                g_free(title);
            }
            offset += len;
        }
        offset = 0;
    }

    if( fxtext ) free(fxtext);
    fxlen = 0;

    load_from = info->uc.expected_num_streams;
    if( load_from < 0 )
        load_from = 0;

    multi_vims( VIMS_STREAM_LIST,"%d",(with_reset_slotselection ? 0 : load_from) );
    fxtext = recv_vims(5, &fxlen);

    if( fxlen > 0 && fxtext != NULL)
    {
        no_samples = 0;
        while( offset < fxlen )
        {
            char tmp_len[4];
            veejay_memset(tmp_len,0,sizeof(tmp_len));
            strncpy(tmp_len, fxtext + offset, 3 );

            int len = atoi(tmp_len);
            offset += 3;
            if(len > 0)
            {
                veejay_memset(line,0,sizeof(line));
                veejay_memset(descr,0,sizeof(descr));
                strncpy( line, fxtext + offset, len );

                int values[10];
                veejay_memset(values,0, sizeof(values));
                sscanf( line, "%05d%02d%03d%03d%03d%03d%03d%03d",
                       &values[0], &values[1], &values[2],
                       &values[3], &values[4], &values[5],
                       &values[6], &values[7]);

                strncpy( descr, line + 22, values[6] );
                switch( values[1] )
                {
                    case STREAM_CALI:
                        snprintf(source,sizeof(source),"calibrate %d",values[0]);
                        break;
                    case STREAM_VIDEO4LINUX:
                        snprintf(source,sizeof(source),"capture %d",values[0]);
                        break;
                    case STREAM_WHITE:
                        snprintf(source,sizeof(source),"solid %d",values[0]);
                        break;
                    case STREAM_MCAST:
                        snprintf(source,sizeof(source),"multicast %d",values[0]);
                        break;
                    case STREAM_NETWORK:
                        snprintf(source,sizeof(source),"unicast %d",values[0]);
                        break;
                    case STREAM_YUV4MPEG:
                        snprintf(source,sizeof(source),"y4m %d",values[0]);
                        break;
                    case STREAM_DV1394:
                        snprintf(source,sizeof(source),"dv1394 %d",values[0]);
                        break;
                    case STREAM_PICTURE:
                        snprintf(source,sizeof(source),"image %d",values[0]);
                        break;
                    case STREAM_GENERATOR:
                        snprintf(source,sizeof(source),"Z%d",values[0]);
                        break;
                    case STREAM_CLONE:
                        snprintf(source,sizeof(source),"T%d", values[0]);
                        break;
                    case STREAM_VLOOP:
                        snprintf(source,sizeof(source),"vloop %d", values[0]);
                        break;
		    case STREAM_AVF:
			snprintf(source,sizeof(source),"stream %d", values[0]);
			break;
                    default:
                        snprintf(source,sizeof(source),"??? %d", values[0]);
                        break;
                }
                gchar *gsource = _utf8str( descr );
                gchar *gtype = _utf8str( source );
                int bank_page = -1;
                int poke_slot = 0;

                verify_bank_capacity( &bank_page , &poke_slot, values[0], values[1]);

                if(bank_page >= 0)
                {
                    if( info->sample_banks[bank_page]->slot[poke_slot]->sample_id <= 0 )
                    {
                        sample_slot_t *tmp_slot = create_temporary_slot(poke_slot,values[0],values[1], gtype,gsource );
                        add_sample_to_sample_banks(bank_page, tmp_slot );
                        add_sample_to_effect_sources_list( values[0], values[1],gsource,gtype);
                        free_slot(tmp_slot);
                    }
                    else
                    {
                        update_sample_slot_data( bank_page, poke_slot, values[0],values[1],gsource,gtype);
                    }
                }
                g_free(gsource);
                g_free(gtype);
            }
            offset += len;
        }

    }

    if(fxtext) free(fxtext);

    select_slot( info->status_tokens[PLAY_MODE] );
    if(no_samples) {
        samplebank_ready_ = 1; // as long as there are no samples, samplebank will not initialize
    }
}

gboolean view_el_selection_func (GtkTreeSelection *selection,
                                 GtkTreeModel     *model,
                                 GtkTreePath      *path,
                                 gboolean          path_currently_selected,
                                 gpointer          userdata)
{
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        gint num = 0;
        gtk_tree_model_get(model, &iter, COLUMN_INT, &num, -1);

        if (!path_currently_selected)
        {
            info->uc.selected_el_entry = num;
            gint frame_num =0;
            frame_num = _el_ref_start_frame( num );
            update_spin_value( "button_el_selstart", frame_num);
            update_spin_value( "button_el_selend", _el_ref_end_frame( num ) );
        }
    }
    return TRUE; /* allow selection state to change */
}

void on_vims_row_activated(GtkTreeView *treeview,
                           GtkTreePath *path,
                           GtkTreeViewColumn *col,
                           gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;
    model = gtk_tree_view_get_model(treeview);
    if(gtk_tree_model_get_iter(model,&iter,path))
    {
        gchar *vimsid = NULL;
        gint event_id =0;
        gtk_tree_model_get(model,&iter, VIMS_ID, &vimsid, -1);

        if(sscanf( vimsid, "%d", &event_id ))
        {
            if(event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END)
            {
                multi_vims( VIMS_BUNDLE, "%d", event_id );
                info->uc.reload_hint[HINT_CHAIN] = 1;
            }
            else
            {
                gchar *args = NULL;
                gchar *format = NULL;
                gtk_tree_model_get(model,&iter, VIMS_FORMAT,  &format, -1);
                gtk_tree_model_get(model,&iter, VIMS_CONTENTS, &args, -1 );

                if( event_id == VIMS_QUIT )
                {
                    if( prompt_dialog("Stop Veejay", "Are you sure  ? (All unsaved work will be lost)" ) ==
                       GTK_RESPONSE_REJECT )
                    return;
                }
                if( (format == NULL||args==NULL) || (strlen(format) <= 0) )
                    single_vims( event_id );
                else
                {
                    if( args != NULL && strlen(args) > 0 )
                    {
                        char msg[100];
                        sprintf(msg, "%03d:%s;", event_id, args );
                        msg_vims(msg);
                    }
                }
            }
        }
        if( vimsid ) g_free( vimsid );
    }
}

gboolean view_vims_selection_func (GtkTreeSelection *selection,
                                   GtkTreeModel     *model,
                                   GtkTreePath      *path,
                                   gboolean          path_currently_selected,
                                   gpointer          userdata)
{
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter(model, &iter, path))
    {
        gchar *vimsid = NULL;
        gint event_id = 0;
        gchar *text = NULL;
        gint n_params = 0;
        gtk_tree_model_get(model, &iter, VIMS_ID, &vimsid, -1);
        gtk_tree_model_get(model, &iter, VIMS_CONTENTS, &text, -1 );
        gtk_tree_model_get(model, &iter, VIMS_PARAMS, &n_params, -1);
        int k=0;
        int m=0;
        gchar *key = NULL;
        gchar *mod = NULL;
#ifdef HAVE_SDL
        gtk_tree_model_get(model,&iter, VIMS_KEY, &key, -1);
        gtk_tree_model_get(model,&iter, VIMS_MOD, &mod, -1);
#endif
        if(sscanf( vimsid, "%d", &event_id ))
        {
#ifdef HAVE_SDL
            k = sdlkey_by_name( key );
            m = sdlmod_by_name( mod );
#endif
            info->uc.selected_vims_entry = event_id;

            if( event_id >= VIMS_BUNDLE_START && event_id < VIMS_BUNDLE_END )
                info->uc.selected_vims_type = 0;
            else
                info->uc.selected_vims_type = 1;

            if(info->uc.selected_vims_args )
                free(info->uc.selected_vims_args);
            info->uc.selected_vims_args = NULL;

            if( n_params > 0 && text )
                info->uc.selected_vims_args = strdup( text );

            info->uc.selected_vims_accel[0] = m;
            info->uc.selected_vims_accel[1] = k;

            clear_textview_buffer( "vimsview" );
            if(text)
                set_textview_buffer( "vimsview", text );
        }
        if(vimsid) g_free( vimsid );
        if(text) g_free( text );
        if(key) g_free( key );
        if(mod) g_free( mod );
    }

    return TRUE; /* allow selection state to change */
}

void
on_editlist_row_activated(GtkTreeView *treeview,
                          GtkTreePath *path,
                          GtkTreeViewColumn *col,
                          gpointer user_data)
{
    GtkTreeModel *model;
    GtkTreeIter iter;

    model = gtk_tree_view_get_model(treeview);
    if(gtk_tree_model_get_iter(model,&iter,path))
    {
        gint num = 0;
        gtk_tree_model_get(model,&iter, COLUMN_INT, &num, -1);
        gint frame_num = _el_ref_start_frame( num );

        multi_vims( VIMS_VIDEO_SET_FRAME, "%d", (int) frame_num );
    }
}

void on_stream_color_changed(GtkColorSelection *colorsel, gpointer user_data)
{
    if(!info->status_lock && info->selected_slot)
    {
        GdkColor current_color;
        GtkWidget *colorsel = glade_xml_get_widget_(info->main_window,
                                                    "colorselection" );
        gtk_color_selection_get_current_color(GTK_COLOR_SELECTION( colorsel ),
                                              &current_color );

        gint red = current_color.red / 255.0;
        gint green = current_color.green / 255.0;
        gint blue = current_color.blue / 255.0;

        multi_vims(VIMS_STREAM_COLOR, "%d %d %d %d",
                   info->selected_slot->sample_id,
                   red,
                   green,
                   blue );
    }

}

static void setup_colorselection()
{
    GtkWidget *sel = glade_xml_get_widget_(info->main_window, "colorselection");
    g_signal_connect(sel,
                     "color-changed",
                     (GCallback) on_stream_color_changed, NULL );
}

void on_rgbkey_color_changed(GtkColorSelection *colorsel, gpointer user_data)
{
    if(!info->entry_lock)
    {
        GdkColor current_color;
        GtkWidget *colorsel = glade_xml_get_widget_(info->main_window,
                                                    "rgbkey" );
        gtk_color_selection_get_current_color(GTK_COLOR_SELECTION( colorsel ),
                                              &current_color );

        // scale to 0 - 255
        gint red = current_color.red / 255.0;
        gint green = current_color.green / 255.0;
        gint blue = current_color.blue / 255.0;

        multi_vims(VIMS_CHAIN_ENTRY_SET_ARG_VAL,
                   "%d %d %d %d",
                   0,
                   info->uc.selected_chain_entry,
                   1,
                   red );
        multi_vims(VIMS_CHAIN_ENTRY_SET_ARG_VAL,
                   "%d %d %d %d",
                   0,
                   info->uc.selected_chain_entry,
                   2,
                   green );
        multi_vims(VIMS_CHAIN_ENTRY_SET_ARG_VAL,
                   "%d %d %d %d",
                   0,
                   info->uc.selected_chain_entry,
                   3,
                   blue );

        info->parameter_lock = 1;
        update_slider_value2(widget_cache[WIDGET_SLIDER_P1], red, 0 );
        update_slider_value2(widget_cache[WIDGET_SLIDER_P2], green, 0 );
        update_slider_value2(widget_cache[WIDGET_SLIDER_P3], blue, 0 );

        info->parameter_lock = 0;
    }
}

static void setup_rgbkey()
{
    GtkWidget *sel = widget_cache[ WIDGET_RGBKEY ];
    g_signal_connect( sel, "color-changed",
    (GCallback) on_rgbkey_color_changed, NULL );
}

static void setup_vimslist()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_vims");
    GtkListStore *store = gtk_list_store_new( 2,G_TYPE_STRING, G_TYPE_STRING);
    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    g_object_unref( G_OBJECT( store ));

    setup_tree_text_column( "tree_vims", VIMS_LIST_ITEM_ID, "VIMS ID",0);
    setup_tree_text_column( "tree_vims", VIMS_LIST_ITEM_DESCR, "Description",0 );

    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

    gtk_tree_sortable_set_sort_func(sortable,
                                    VIMS_ID,
                                    sort_vims_func,
                                    GINT_TO_POINTER(VIMS_ID),
                                    NULL);

    gtk_tree_sortable_set_sort_column_id(sortable,
                                         VIMS_ID, GTK_SORT_ASCENDING);
}

static void setup_bundles()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_bundles");
    GtkListStore *store = gtk_list_store_new( 7,G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING ,G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

    gtk_widget_set_size_request_( tree, 300, -1 );

    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    GtkTreeSortable *sortable = GTK_TREE_SORTABLE(store);

    gtk_tree_sortable_set_sort_func(
        sortable, VIMS_ID, sort_vims_func,
            GINT_TO_POINTER(VIMS_ID),NULL);

    gtk_tree_sortable_set_sort_column_id( sortable, VIMS_ID, GTK_SORT_ASCENDING);

    g_object_unref( G_OBJECT( store ));

    setup_tree_text_column( "tree_bundles", VIMS_ID, "Event ID",0);
    setup_tree_text_column( "tree_bundles", VIMS_KEY, "Key",0);
    setup_tree_text_column( "tree_bundles", VIMS_MOD, "Mod",0);
    setup_tree_text_column( "tree_bundles", VIMS_DESCR, "Description",0 );
    setup_tree_text_column( "tree_bundles", VIMS_PARAMS, "Max args",0);
    setup_tree_text_column( "tree_bundles", VIMS_FORMAT, "Format",0 );
    g_signal_connect(tree,
                     "row-activated",
                     (GCallback) on_vims_row_activated,
                     NULL );

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(tree));
    gtk_tree_selection_set_select_function(selection, view_vims_selection_func, NULL, NULL);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);

    GtkWidget *tv = glade_xml_get_widget_( info->main_window, "vimsview" );
    gtk_text_view_set_wrap_mode( GTK_TEXT_VIEW(tv), GTK_WRAP_WORD_CHAR );
}

static void setup_editlist_info()
{
    editlist_tree = glade_xml_get_widget_( info->main_window, "editlisttree");
    editlist_store = gtk_list_store_new( 5,G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING ,G_TYPE_STRING);
    gtk_tree_view_set_model( GTK_TREE_VIEW(editlist_tree), GTK_TREE_MODEL(editlist_store));
    g_object_unref( G_OBJECT( editlist_store ));
    editlist_model = gtk_tree_view_get_model( GTK_TREE_VIEW(editlist_tree ));
    editlist_store = GTK_LIST_STORE(editlist_model);

    setup_tree_text_column( "editlisttree", COLUMN_INT, "#",0);
    setup_tree_text_column( "editlisttree", COLUMN_STRING0, "Timecode",0 );
    setup_tree_text_column( "editlisttree", COLUMN_STRINGA, "Filename",0);
    setup_tree_text_column( "editlisttree", COLUMN_STRINGB, "Duration",0);
    setup_tree_text_column( "editlisttree", COLUMN_STRINGC, "FOURCC",0);

    g_signal_connect( editlist_tree, "row-activated",
    (GCallback) on_editlist_row_activated, NULL );

    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(editlist_tree));
    gtk_tree_selection_set_select_function(selection, view_el_selection_func, NULL, NULL);
    gtk_tree_selection_set_mode(selection, GTK_SELECTION_SINGLE);
}

static void reload_keys()
{
    gint len = 0;
    single_vims( VIMS_KEYLIST );
    gchar *text = recv_vims( 6, &len );
    gint offset = 0;

    int checksum = data_checksum(text,len);
    if( info->uc.reload_hint_checksums[HINT_KEYS] == checksum ) {
        if(text) free(text);
        return;
    }
    info->uc.reload_hint_checksums[HINT_KEYS] = checksum;

    if( len == 0 || text == NULL )
        return;

    gint k,index;
    for( k = 0; k < VIMS_MAX  ; k ++ )
    {
        vims_keys_t *p = &vims_keys_list[k];
        if(p->vims)
            free(p->vims);
        p->keyval = 0;
        p->state = 0;
        p->event_id = 0;
        p->vims = NULL;
    }

    char *ptr = text;

    while( offset < len )
    {
        int val[6];
        veejay_memset(val,0,sizeof(val));
        int n = sscanf( ptr + offset, "%04d%03d%03d%03d", &val[0],&val[1],&val[2],&val[3]);
        if( n != 4 )
        {
            free(text);
            return;
        }

        offset += 13;
        char *message = strndup( ptr + offset , val[3] );

        offset += val[3];

        index = (val[1] * G_MOD_OFFSET) + val[2];

        if( index < 0 || index >= VIMS_MAX ) {
         	free(message);
	     	continue;
	}

        vims_keys_list[ index ].keyval      = val[2];
        vims_keys_list[ index ].state       = val[1];
        vims_keys_list[ index ].event_id    = val[0];
        vims_keys_list[ index ].vims        = message;
    }
    free(text);
}

static void reload_bundles()
{
    reload_keys();

    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_bundles");
    GtkListStore *store;
    GtkTreeIter iter;

    gint len = 0;
    single_vims( VIMS_BUNDLE_LIST );
    gchar *eltext = recv_vims(6,&len); // msg len
    gint offset = 0;

    int checksum = data_checksum(eltext,len);
    if( info->uc.reload_hint_checksums[HINT_BUNDLES] == checksum ) {
        if( eltext) free(eltext);
        return;
    }
    info->uc.reload_hint_checksums[HINT_BUNDLES] = checksum;

    reset_tree("tree_bundles");

    if(len == 0 || eltext == NULL )
    {
#ifdef STRICT_CHECKING
        assert(eltext != NULL && len > 0);
#endif
        return;
    }

    GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
    store = GTK_LIST_STORE(model);

    char *ptr = eltext;

    while( offset < len )
    {
        char *message = NULL;
        char *format  = NULL;
        char *args    = NULL;
        int val[6] = { 0,0,0,0,0,0 };

        if(sscanf( ptr + offset, "%04d%03d%03d%04d", &val[0],&val[1],&val[2],&val[3]) != 4 ) {
            veejay_msg(VEEJAY_MSG_DEBUG,"%s: Unexpected input at byte %d",__FUNCTION__, offset );
            free(eltext);
            return;
        }

        offset += 14;

        message = strndup( ptr + offset , val[3] );

        offset += val[3];

        if( sscanf( ptr + offset, "%03d%03d", &val[4], &val[5] ) != 2 ) {
            veejay_msg(VEEJAY_MSG_DEBUG,"%s: Unexpected input at byte %d",__FUNCTION__, offset );
            free(eltext);
            return;
        }

        offset += 6;

        if(val[4]) // format string
        {
            format = strndup( ptr + offset, val[4] );
            offset += val[4];
        }

        if(val[5]) // argument string
        {
            args   = strndup( ptr + offset, val[5] );
            offset += val[5];
        }

        gchar *g_descr  = NULL;
        gchar *g_format = NULL;
        gchar *g_content = NULL;
#ifdef HAVE_SDL
        gchar *g_keyname  = sdlkey_by_id( val[1] );
        gchar *g_keymod   = sdlmod_by_id( val[2] );
#else
        gchar *g_keyname = "N/A";
        gchar *g_keymod = "";
#endif
        gchar *g_vims[5];

        snprintf( (char*) g_vims,sizeof(g_vims), "%03d", val[0] );

        if( val[0] >= VIMS_BUNDLE_START && val[0] < VIMS_BUNDLE_END )
        {
            g_content = _utf8str( message );
	        g_descr = _utf8str("Bundle");
        }
        else
        {
            g_descr = _utf8str( message );
            if( format )
                g_format = _utf8str( format );
            if( args )
            {
                g_content = _utf8str( args );
        //@ set default VIMS argument:
                if(vj_event_list[val[0]].args )
                {
                    free(vj_event_list[val[0]].args );
                    vj_event_list[val[0]].args = NULL;
                }
                vj_event_list[ val[0] ].args = strdup( args );
            }
        }
        
        gtk_list_store_append( store, &iter );
        gtk_list_store_set(store, &iter,
                           VIMS_ID,     g_vims,
                           VIMS_KEY,    g_keyname,
                           VIMS_MOD,    g_keymod,
 			               VIMS_DESCR,  g_descr,
                           VIMS_PARAMS,     vj_event_list[ val[0] ].params,
                           VIMS_FORMAT,     g_format,
                           VIMS_CONTENTS,  g_content, /* this is a hidden column, when the item is selected, the text is displayed in the textview widget */
                           -1 );

        if(message) free(message);
        if(format)  free(format);
        if(args)    free(args);

        if( g_descr ) g_free(g_descr );
        if( g_format ) g_free(g_format );
        if( g_content) g_free(g_content );
    }
    /* entry, start frame, end frame */

    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    free( eltext );
}

static void reload_vimslist()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "tree_vims");
    GtkListStore *store;
    GtkTreeIter iter;

    gint len = 0;
    single_vims( VIMS_VIMS_LIST );
    gchar *eltext = recv_vims(5,&len); // msg len
    gint offset = 0;
    reset_tree("tree_vims");

    if(len == 0 || eltext == NULL )
    {
#ifdef STRICT_CHECKING
        assert(eltext != NULL && len > 0);
#endif
        return;
    }

    GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
    store = GTK_LIST_STORE(model);

    while( offset < len )
    {
        char *format = NULL;
        char *descr = NULL;
        char *line = strndup( eltext + offset, 12 );
        int val[4];
        if( sscanf(line, "%04d%02d%03d%03d",
               &val[0],&val[1],&val[2],&val[3]) != 4 ) {
            veejay_msg(0,"Expected exactly 4 tokens");
        }

        if( val[0] < 0 || val[0] > 1024 ) {
            veejay_msg(0,"Invalid ID at position %d", offset );
        }

        if( val[1] < 0 || val[1] > 99 ) {
            veejay_msg(0, "Invalid number of arguments at position %d", offset);
        }

        if( val[2] < 0 || val[2] > 999 ) {
            veejay_msg(0, "Invalid format length at position %d", offset );
        }

        if( val[3] < 0 || val[3] > 999 ) {
            veejay_msg(0, "Invalid name length at position %d", offset );
        }

        char vimsid[5];

        offset += 12;
        if(val[2] > 0)
        {
            format = strndup( eltext + offset, val[2] );
            offset += val[2];
        }

        if(val[3] > 0 )
        {
            descr = strndup( eltext + offset, val[3] );
            offset += val[3];
        }


        if(vj_event_list[val[0]].format )
            free(vj_event_list[val[0]].format);
        if(vj_event_list[val[0]].descr )
            free(vj_event_list[val[0]].descr);

        gtk_list_store_append( store, &iter );

        vj_event_list[ val[0] ].event_id = val[0];
        vj_event_list[ val[0] ].params   = val[1];
        vj_event_list[ val[0] ].format   = format; 
        vj_event_list[ val[0] ].descr    = descr;

        sprintf(vimsid, "%03d", val[0] );
        gtk_list_store_set(store, &iter,
                           VIMS_LIST_ITEM_ID, vimsid,
                           VIMS_LIST_ITEM_DESCR,descr,-1 );

        free( line );
    }

    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
    free( eltext );
}

static char *tokenize_on_space( char *q )
{
    int n = 0;
    char *r = NULL;
    char *p = q;
    while( *p != '\0' && !isblank( *p ) && *p != ' ' && *p != 20)
    {
        (*p)++;
        n++;
    }
    if( n <= 0 )
        return NULL;
    r = vj_calloc( n+1 );
    strncpy( r, q, n );
    return r;
}

static int have_srt_ = 0;

static void init_srt_editor()
{
    reload_fontlist();
    update_spin_range( "spin_text_x", 0, info->el.width-1 , 0 );
    update_spin_range( "spin_text_y", 0, info->el.height-1, 0 );
    update_spin_range( "spin_text_size", 10, 500, 40 );
    update_spin_range( "spin_text_start", 0, total_frames_, 0 );
}

static void reload_fontlist()
{
    GtkWidget *box = glade_xml_get_widget_( info->main_window, "combobox_fonts");
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT( box ) );

    single_vims( VIMS_FONT_LIST );
    gint len = 0;
    gchar *srts = recv_vims(6,&len );
    gint i = 0;
    gchar *p = srts;

    while( i < len )
    {
        char tmp[4];
        veejay_memset(tmp,0,sizeof(tmp));
        strncpy(tmp, p, 3 );
        int slen = atoi(tmp);
        p += 3;
        gchar *seq_str = strndup( p, slen );
        gtk_combo_box_text_append( GTK_COMBO_BOX_TEXT(box), NULL, seq_str );
        p += slen;
        free(seq_str);
        i += (slen + 3);
    }
    free(srts);
}

static void reload_srt()
{
    if(!have_srt_)
    {
        init_srt_editor();
        have_srt_ = 1;
    }

    GtkWidget *box = glade_xml_get_widget_( info->main_window, "combobox_textsrt");
    gtk_combo_box_text_remove_all (GTK_COMBO_BOX_TEXT( box ) );

    clear_textview_buffer( "textview_text");

    single_vims( VIMS_SRT_LIST );
    gint i=0, len = 0;

    gchar *srts = recv_vims(6,&len );
    if( srts == NULL || len <= 0 )
    {
    //  disable_widget( "SRTframe" );
        return;
    }

    gchar *p = srts;
    gchar *token = NULL;

    while(  i < len )
    {
        token = tokenize_on_space( p );
        if(!token)
            break;
        if(token)
        {
            gtk_combo_box_text_append( GTK_COMBO_BOX_TEXT(box), NULL, token );
            i += strlen(token) + 1;
            free(token);
        }
        else
            i++;
        p = srts + i;
    }
    free(srts);
}

void _edl_reset(void)
{
    if( info->elref != NULL)
    {
        int n = g_list_length(info->elref);
        int i;
        for( i = 0; i <=n ; i ++ )
        {
            void *ptr = g_list_nth_data( info->elref , i );
            if(ptr)
                free(ptr);
        }
        g_list_free( info->elref );
    }
}

void	reload_macros()
{
	GtkWidget *tree = glade_xml_get_widget_( info->main_window, "macro_macros" );
	GtkListStore *store;
	GtkTreeIter iter;

	gint consumed = 0;
	gint len = 0;
	single_vims( VIMS_GET_ALL_MACRO );
	gchar *answer = recv_vims(8, &len);

    int checksum = data_checksum(answer,len);
    if( info->uc.reload_hint_checksums[HINT_MACRO] == checksum ) {
        if(answer) free(answer);
        return;
    }
    info->uc.reload_hint_checksums[HINT_MACRO] = checksum;


	reset_tree("macro_macros");

	if( answer == NULL || len < 0 )
		return;

	GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
    	store = GTK_LIST_STORE(model);

	gchar *ptr = answer;

	int error = 0;

	while(consumed < len) {
		
		long frame_num = 0;
		int at_dup = 0;
		int at_loop = 0;
		int at_seq = 0;
		int msg_len = 0;

		int n = sscanf( ptr, "%08ld%02d%08d%02d%03d", &frame_num, &at_dup, &at_loop, &at_seq, &msg_len );
		if( n != 5) {
			error = 1;
			break;
		}

		ptr += (8 + 2 + 8 + 2 + 3);

		char *msg = strndup( ptr, msg_len );
		int vims_id = -1;
		n = sscanf(msg,"%03d:", &vims_id);
		char *descr = NULL;
		if( vims_id >= 0 && vims_id < VIMS_QUIT )
			descr = vj_event_list[vims_id].descr;
	
		gtk_list_store_append( store, &iter );
    		gtk_list_store_set(store, &iter,
    		MACRO_FRAME, (guint) frame_num,
			MACRO_DUP, (guint) at_dup,
			MACRO_LOOP, (guint) at_loop,
			MACRO_MSG_SEQ, (guint) at_seq,
			MACRO_VIMS, msg, 
			MACRO_VIMS_DESCR, (descr == NULL ? "Unknown": descr),
			-1 );

		ptr += msg_len;

		free(msg);

		consumed += (8 + 2 + 8 + 2 + 3);
		consumed += msg_len;
	}

	if(error){
		veejay_msg(0,"Unable to read all VIMS macros");
	}

	gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));
		
	free(answer);

	info->uc.reload_hint[HINT_MACRO] = 0;
}

static void reload_editlist_contents()
{
    GtkWidget *tree = glade_xml_get_widget_( info->main_window, "editlisttree");
    GtkListStore *store;
    GtkTreeIter iter;

    gint i;
    gint len = 0;
    single_vims( VIMS_EDITLIST_LIST );
    gchar *eltext = recv_vims(6,&len); // msg len
    gint offset = 0;
    gint num_files=0;
    reset_tree("editlisttree");
    _el_ref_reset();
    _el_entry_reset();
    _edl_reset();

    if( eltext == NULL || len < 0 )
    {
        return;
    }

    el_constr *el;

    char *tmp = strndup( eltext + offset, 4 );
    if( sscanf( tmp,"%d",&num_files ) != 1 )
    {
        free(tmp);
        free(eltext);
        return;
    }
    free(tmp);

    offset += 4;

    for( i = 0; i < num_files ; i ++ )
    {
        int name_len = 0;
        tmp = strndup( eltext + offset, 4 );
        if( sscanf( tmp,"%d", &name_len ) != 1 )
        {
            free(tmp);
            free(eltext);
            return;
        }
        offset += 4;
        free(tmp);
        char *file = strndup( eltext + offset, name_len );

        offset += name_len;
        int iter = 0;
        tmp = strndup( eltext + offset, 4 );
        if( sscanf( tmp, "%d", &iter ) != 1 )
        {
            free(tmp);
            free(eltext);
            return;
        }
        free(tmp);
        offset += 4;

        long num_frames = 0;
        tmp = strndup( eltext + offset, 10 );
        if( sscanf(tmp, "%ld", &num_frames ) != 1 )
        {
            free(tmp);
            free(eltext);
            return;
        }
        free(tmp);
        offset += 10;

        int fourcc_len = 0;
        tmp = strndup( eltext + offset, 2 );
        if( sscanf( tmp, "%d", &fourcc_len) != 1 )
        {
            free(tmp);
            free(eltext);
            return;
        }
        offset += fourcc_len;
        char *fourcc = strndup( eltext + offset - 1, fourcc_len );

        el = _el_entry_new( iter, file, num_frames, fourcc );
        info->editlist = g_list_append( info->editlist, el );

        offset += 2;

        free(file);
        free(fourcc);
        free(tmp);
    }
    GtkTreeModel *model = gtk_tree_view_get_model( GTK_TREE_VIEW(tree ));
    store = GTK_LIST_STORE(model);

    int total_frames = 0; // running total of frames
    int row_num = 0;
    while( offset < len )
    {
        tmp = (char*)strndup( eltext + offset, (3*16) );
        offset += (3*16);
        long nl=0, n1=0,n2=0;

        sscanf( tmp, "%016ld%016ld%016ld",
            &nl,&n1,&n2 );

        if(nl < 0 || nl >= num_files)
        {
            free(tmp);
            free(eltext);
            return;
        }
        int file_len = _el_get_nframes( nl );
        if(file_len <= 0)
        {
            free(tmp);
            row_num++;
            continue;
        }
        if(n1 < 0 )
            n1 = 0;
        if(n2 >= file_len)
            n2 = file_len;

        if(n2 <= n1 )
        {
            free(tmp);
            row_num++;
            continue;
        }

        info->elref = g_list_append( info->elref, _el_ref_new( row_num,(int) nl,n1,n2,total_frames ) ) ;
        char *tmpname = _el_get_filename(nl);
        gchar *fname = get_relative_path(tmpname);
        char *timecode = format_selection_time( n1,n2 );
        gchar *gfourcc = _utf8str( _el_get_fourcc(nl) );
        gchar *timeline = format_selection_time( 0, total_frames );

        gtk_list_store_append( store, &iter );
        gtk_list_store_set(store, &iter,
                           COLUMN_INT, (guint) row_num,
                           COLUMN_STRING0, timeline,
                           COLUMN_STRINGA, fname,
                           COLUMN_STRINGB, timecode,
                           COLUMN_STRINGC, gfourcc,-1 );

        free(timecode);
        g_free(gfourcc);
        g_free(fname);
        free(timeline);
        free(tmp);

        total_frames = total_frames + (n2-n1) + 1;
        row_num ++;
    }

    gtk_tree_view_set_model( GTK_TREE_VIEW(tree), GTK_TREE_MODEL(store));

    free( eltext );
}

// execute after el change:
static void load_editlist_info()
{
    char norm;
    float fps;
    int values[10] = { 0 };
    long rate = 0;
    long dum[2];
    char tmp[16];
    int len = 0;

    single_vims( VIMS_VIDEO_INFORMATION );
    gchar *res = recv_vims(3,&len);
    if( len <= 0 || res==NULL)
    {
#ifdef STRICT_CHECKING
        assert(len > 0 && res != NULL);
#endif
        return;
    }
    sscanf(res, "%d %d %d %c %f %d %d %ld %d %ld %ld %d %d",
           &values[0], &values[1], &values[2], &norm,&fps,
           &values[4], &values[5], &rate, &values[7],
           &dum[0], &dum[1], &values[8], &use_vims_mcast);
    snprintf( tmp, sizeof(tmp)-1, "%dx%d", values[0],values[1]);

    info->el.width = values[0];
    info->el.height = values[1];
    info->el.num_frames = dum[1];
    update_label_str( "label_el_wh", tmp );
    snprintf( tmp, sizeof(tmp)-1, "%s",
        (norm == 'p' ? "PAL" : "NTSC" ) );
    update_label_str( "label_el_norm", tmp);
    update_label_f( "label_el_fps", fps );

    update_spin_value( "screenshot_width", info->el.width );
    update_spin_value( "screenshot_height", info->el.height );

    info->el.fps = fps;
#ifdef STRICT_CHECKING
    assert( info->el.fps > 0 );
#endif
    info->el.num_files = dum[0];
    snprintf( tmp, sizeof(tmp)-1, "%s",
        ( values[2] == 0 ? "progressive" : (values[2] == 1 ? "top first" : "bottom first" ) ) );
    update_label_str( "label_el_inter", tmp );
    update_label_i( "label_el_arate", (int)rate, 0);
    update_label_i( "label_el_achans", values[7], 0);
    update_label_i( "label_el_abits", values[5], 0);

    if( values[4] == 0 )
    {
        if(gtk_widget_is_sensitive( widget_cache[ WIDGET_BUTTON_5_4 ] ))
            gtk_widget_set_sensitive(widget_cache[ WIDGET_BUTTON_5_4 ], FALSE);
    }
    else
    {
        if(!gtk_widget_is_sensitive(widget_cache[ WIDGET_BUTTON_5_4 ] ))
            gtk_widget_set_sensitive(widget_cache[ WIDGET_BUTTON_5_4 ], TRUE);
        if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON( widget_cache[ WIDGET_BUTTON_5_4 ] )) != values[8] )
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( widget_cache[ WIDGET_BUTTON_5_4 ] ) , values[8] );
    }

    if( use_vims_mcast ) {
        if(!gtk_widget_is_sensitive(widget_cache[ WIDGET_TOGGLE_MULTICAST] ))
            gtk_widget_set_sensitive(widget_cache[ WIDGET_TOGGLE_MULTICAST ], TRUE);
    }
    else {
         if(gtk_widget_is_sensitive(widget_cache[ WIDGET_TOGGLE_MULTICAST ] ))
            gtk_widget_set_sensitive(widget_cache[ WIDGET_TOGGLE_MULTICAST ], FALSE);
    }

    free(res);
}

static void notebook_set_page(const char *name, int page)
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
    if(!w)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Widget '%s' not found",name);
        return;
    }
    gtk_notebook_set_current_page( GTK_NOTEBOOK(w), page );
}

static void hide_widget(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
    if(!w) {
        veejay_msg(VEEJAY_MSG_ERROR, "Widget '%s' not found",name);
        return;
    }
    gtk_widget_hide(w);
}

static void show_widget(const char *name)
{
    GtkWidget *w = glade_xml_get_widget_(info->main_window,name);
    if(!w)
    {
        veejay_msg(VEEJAY_MSG_ERROR, "Widget '%s' not found",name);
        return;
    }
    gtk_widget_show(w);
}

char *format_selection_time(int start, int end)
{
    double fps = (double) info->el.fps;
    int pos = (end-start);

    return format_time( pos, fps );
}

static gboolean update_cpumeter_timeout( gpointer data )
{
    gdouble ms   = (gdouble)info->status_tokens[ELAPSED_TIME];
    //gdouble fs   = (gdouble)get_slider_val( "framerate" );
    gdouble fs = (gdouble) info->status_tokens[18] * 0.01;
    gdouble lim  = (1.0f/fs)*1000.0;

    if( ms < lim )
    {
        update_label_str( "cpumeter", text_msg_[TEXT_REALTIME].text );
    } 
    else
    {
        char text[32];
        sprintf(text, "%2.2f FPS", ( 1.0f / ms ) * 1000.0 );

        update_label_str( "cpumeter", text );
    }
    return TRUE;
}

static gboolean update_cachemeter_timeout( gpointer data )
{
    char text[32];
    gint v = info->status_tokens[TOTAL_MEM];
    sprintf(text,"%d MB cached",v);
    update_label_str( "cachemeter", text );

    return TRUE;
}

static gboolean update_sample_record_timeout(gpointer data)
{
    if( info->uc.playmode == MODE_SAMPLE )
    {
        GtkWidget *w;
        if( is_button_toggled("seqactive" ) )
        {
            w = glade_xml_get_widget_( info->main_window,
                                      "rec_seq_progress" );
        }
        else
        {
            w = glade_xml_get_widget_( info->main_window,
                                      "samplerecord_progress" );

        }
        gdouble tf = info->status_tokens[STREAM_DURATION];
        gdouble cf = info->status_tokens[STREAM_RECORDED];

        gdouble fraction = cf / tf;

        if(!info->status_tokens[STREAM_RECORDING] )
        {
            gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), 0.0);
            info->samplerecording = 0;
            info->uc.recording[MODE_SAMPLE] = 0;
            if(info->uc.render_record)
            {
                info->uc.render_record = 0;  // render list has private edl
            }
            else
            {
                info->uc.reload_hint[HINT_EL] = 1;
            }
            return FALSE;
        }
        else
        {
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w),
                                          fraction );
        }
    }
    return TRUE;
}

static gboolean update_stream_record_timeout(gpointer data)
{
    GtkWidget *w = glade_xml_get_widget_( info->main_window,
                                         "streamrecord_progress" );
    if( info->uc.playmode == MODE_STREAM )
    {
        gdouble tf = info->status_tokens[STREAM_DURATION];
        gdouble cf = info->status_tokens[STREAM_RECORDED];

        gdouble fraction = cf / tf;
        if(!info->status_tokens[STREAM_RECORDING] )
        {
            gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(w), 0.0);
            info->streamrecording = 0;
            info->uc.recording[MODE_STREAM] = 0;
            info->uc.reload_hint[HINT_EL] = 1; // recording finished, reload edl
            return FALSE;
        }
        else
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(w),
                                          fraction );

    }
    return TRUE;
}

static void init_recorder(int total_frames, gint mode)
{
    if(mode == MODE_STREAM)
    {
        info->streamrecording = g_timeout_add(300, update_stream_record_timeout, (gpointer*) info );
    }
    if(mode == MODE_SAMPLE)
    {
        info->samplerecording = g_timeout_add(300, update_sample_record_timeout, (gpointer*) info );
    }
    info->uc.recording[mode] = 1;
}

static char glade_path[1024];

char *get_glade_path()
{
    return glade_path;
}

char *get_gveejay_dir()
{
    return RELOADED_DATADIR;
}

void get_gd(char *buf, char *suf, const char *filename)
{
    const char *dir = RELOADED_DATADIR;

    if(filename !=NULL && suf != NULL)
        sprintf(buf, "%s/%s/%s",dir,suf, filename );
    if(filename !=NULL && suf==NULL)
        sprintf(buf, "%s/%s", dir, filename);
    if(filename == NULL && suf != NULL)
        sprintf(buf, "%s/%s/" , dir, suf);
}

GdkPixbuf   *vj_gdk_pixbuf_scale_simple( GdkPixbuf *src, int dw, int dh, GdkInterpType inter_type )
{
    return gdk_pixbuf_scale_simple( src,dw,dh,inter_type );
/*
    GdkPixbuf *res = gdk_pixbuf_new( GDK_COLORSPACE_RGB, FALSE, 8, dw, dh );
#ifdef STRICT_CHECKING
    assert( GDK_IS_PIXBUF( res ) );
#endif
    uint8_t *res_out = gdk_pixbuf_get_pixels( res );
    uint8_t *src_in  = gdk_pixbuf_get_pixels( src );
    uint32_t src_w   = gdk_pixbuf_get_width( src );
    uint32_t src_h   = gdk_pixbuf_get_height( src );
    int dst_w = gdk_pixbuf_get_width( res );
    int dst_h = gdk_pixbuf_get_height( res );
    VJFrame *src1 = yuv_rgb_template( src_in, src_w, src_h, PIX_FMT_BGR24 );
    VJFrame *dst1 = yuv_rgb_template( res_out, dst_w, dst_h, PIX_FMT_BGR24 );

    veejay_msg(VEEJAY_MSG_ERROR, "%s: %dx%d -> %dx%d", __FUNCTION__, src_w,src_h,dst_w,dst_h );

    yuv_convert_any_ac( src1,dst1, src1->format, dst1->format );

    free(src1);
    free(dst1);

    return res;*/
}

void gveejay_sleep( void *u )
{
    struct timespec nsecsleep;
    nsecsleep.tv_nsec = 500000;
    nsecsleep.tv_sec = 0;
    nanosleep( &nsecsleep, NULL );
}

//static int tick = 0;
int gveejay_time_to_sync( void *ptr )
{
    vj_gui_t *ui = (vj_gui_t*) ptr;
    struct timeval time_now;
    gettimeofday( &time_now, 0 );

    double diff = time_now.tv_sec - ui->time_last.tv_sec +
            (time_now.tv_usec - ui->time_last.tv_usec ) * 1.e-6;
    
    float fps = 0.0;
    int ret = 0;

    if ( ui->watch.state == STATE_PLAYING )
    {
	    fps = ui->el.fps;
	    float spvf = 1.0f / fps;
        if( diff > spvf ) {
           // veejay_msg(0, "%d diff = %g, delay=%f, ela=%f, spvf=%f", tick++, diff, 0.0,ela,spvf);
	        ret = 1;
        }
    } 
  
    return ret;
}

int veejay_update_multitrack( void *ptr )
{
    sync_info *s = multitrack_sync( info->mt );
/*
    if( s->status_list[s->master] == NULL ) {
        info->watch.w_state = STATE_STOPPED;
        free(s->status_list);
        free(s->img_list );
        free(s->widths);
        free(s->heights);
        free(s);
        
        gettimeofday( &(info->time_last) , 0 );

        return 1;
    }*/

    GtkWidget *maintrack = widget_cache[ WIDGET_IMAGEA ];
    int i;
    GtkWidget *ww = widget_cache[ WIDGET_NOTEBOOK18 ];
    int deckpage = gtk_notebook_get_current_page(GTK_NOTEBOOK(ww));

#ifdef STRICT_CHECKING
    assert( s->status_list[s->master] != NULL );
#endif

    int tmp = 0;

    if( s->status_list[s->master] != NULL ) {
        for ( i = 0; i < STATUS_TOKENS; i ++ )
        {
            tmp += s->status_list[s->master][i];
            info->status_tokens[i] = s->status_list[s->master][i];
        }
    }

    if( tmp == 0 )
    {
        free(s->status_list);
        free(s->img_list );
        free(s->widths);
        free(s->heights);
        free(s);
        
        gettimeofday( &(info->time_last) , 0 );

        return 0;
    }

    info->status_lock = 1;
    info->uc.playmode = info->status_tokens[ PLAY_MODE ];
    update_gui();
    info->prev_mode = info->status_tokens[ PLAY_MODE ];
   
    int pm = info->status_tokens[PLAY_MODE];
#ifdef STRICT_CHECKING
    assert( pm >= 0 && pm < 4 );
#endif
    int *history = info->history_tokens[pm];

    veejay_memcpy( history, info->status_tokens, sizeof(int) * STATUS_TOKENS );
   
    for( i = 0; i < s->tracks ; i ++ )
    {
        if( s->status_list[i] )
        {
            update_multitrack_widgets( info->mt, s->status_list[i], i );

            free(s->status_list[i]);
        }
        if( s->img_list[i] )
        {
            if( i == s->master )
            {
#ifdef STRICT_CHECKING
                assert( s->widths[i] > 0 );
                assert( s->heights[i] > 0 );
                assert( GDK_IS_PIXBUF( s->img_list[i] ) );
#endif
                if( gdk_pixbuf_get_height(s->img_list[i]) == preview_box_w_ &&
                    gdk_pixbuf_get_width(s->img_list[i]) == preview_box_h_  )
                    gtk_image_set_from_pixbuf_( GTK_IMAGE( maintrack ), s->img_list[i] );
                else
                {
                    GdkPixbuf *result = vj_gdk_pixbuf_scale_simple( s->img_list[i],preview_box_w_,preview_box_h_, GDK_INTERP_NEAREST );
                    if(result)
                    {
                        gtk_image_set_from_pixbuf_( GTK_IMAGE( maintrack ), result );
                        g_object_unref(result);
                    }
                }

                if( history[PLAY_MODE] == info->status_tokens[PLAY_MODE] &&
                    history[CURRENT_ID] == info->status_tokens[CURRENT_ID] ) {
                    vj_img_cb( s->img_list[i] );
                }
            }

            if(deckpage == 3) // notebook18 >>> Multitrack page
                multitrack_update_sequence_image( info->mt, i, s->img_list[i] );

            if( s->img_list[i] )
                g_object_unref( s->img_list[i] );
        } 
    }

    info->status_lock = 0;

    free(s->status_list);
    free(s->img_list );
    free(s->widths);
    free(s->heights);
    free(s);
    
    gettimeofday( &(info->time_last) , 0 );

    return 1;
}

static void update_status_accessibility(int old_pm, int new_pm)
{
    int i;

    if( old_pm == new_pm )
        return;

    if( new_pm == MODE_STREAM )
    {
        for(i=0; sample_widget_map[i].sample_widget_id != SAMPLE_WIDGET_NONE; i ++ ) {
            if(gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ sample_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ sample_widget_map[i].widget_id ] ), FALSE);
            }
        }

        for(i=0; plain_widget_map[i].plain_widget_id != PLAIN_WIDGET_NONE; i ++ ) {
            if(gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ plain_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ plain_widget_map[i].widget_id ] ), FALSE);
            }
        }
        
        for(i=0; stream_widget_map[i].stream_widget_id != STREAM_WIDGET_NONE; i ++ ) {
            if(!gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ stream_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ stream_widget_map[i].widget_id ] ), TRUE);
            }
        }
    }

    if( new_pm == MODE_SAMPLE )
    {
        for(i=0; stream_widget_map[i].stream_widget_id != STREAM_WIDGET_NONE; i ++ ) {
            if(gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ stream_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ stream_widget_map[i].widget_id ] ), FALSE);
            }
        }

        for(i=0; plain_widget_map[i].plain_widget_id != PLAIN_WIDGET_NONE; i ++ ) {
            if(gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ plain_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ plain_widget_map[i].widget_id ] ), FALSE);
            }
        }

        for(i=0; sample_widget_map[i].sample_widget_id != SAMPLE_WIDGET_NONE; i ++ ) {
            if(!gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ sample_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ sample_widget_map[i].widget_id ] ), TRUE);
            }
        }

    }

    if( new_pm == MODE_PLAIN)
    {
        for(i=0; stream_widget_map[i].stream_widget_id != STREAM_WIDGET_NONE; i ++ ) {
            if(gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ stream_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ stream_widget_map[i].widget_id ] ), FALSE);
            }
        }

        for(i=0; sample_widget_map[i].sample_widget_id != SAMPLE_WIDGET_NONE; i ++ ) {
            if(gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ sample_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ sample_widget_map[i].widget_id ] ), FALSE);
            }
        }

        for(i=0; plain_widget_map[i].plain_widget_id != PLAIN_WIDGET_NONE; i ++ ) {
            if(!gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[ plain_widget_map[i].widget_id ] )) ) {
                gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[ plain_widget_map[i].widget_id ] ), TRUE);
            }
        }

    }

    GtkWidget *n = widget_cache[ WIDGET_PANELS ];
    int page_needed = 0;
    switch( new_pm )
    {
        case MODE_SAMPLE:
            page_needed =0 ; break;
        case MODE_STREAM:
            page_needed = 1; break;
        case MODE_PLAIN:
            page_needed = 2; break;
        default:
            break;
    }
    gtk_notebook_set_current_page( GTK_NOTEBOOK(n), page_needed );
}

static void set_pm_page_label(int sample_id, int type)
{
    gchar ostitle[100];
    gchar ftitle[100];
    switch(type)
    {
        case 0:
            snprintf(ostitle, sizeof(ostitle), "Sample %d",sample_id);break;
        case 1:
            snprintf(ostitle, sizeof(ostitle), "Stream %d",sample_id);break;
        default:
            snprintf(ostitle,sizeof(ostitle), "Plain");break;
    }
    gchar *title = _utf8str(ostitle);
    snprintf(ftitle,sizeof(ftitle), "<b>%s</b>", ostitle);
    label_set_markup( "label_current_mode", ftitle);
    update_label_str( "label_currentsource", (type == 0 ? "Sample" : "Stream" ) );
    g_free(title);
}

static void update_globalinfo(int *history, int pm, int last_pm)
{
    int i;
    total_frames_ = (pm == MODE_STREAM ? info->status_tokens[SAMPLE_MARKER_END] : info->status_tokens[TOTAL_FRAMES] );
    gint history_frames_ = (pm == MODE_STREAM ? history[SAMPLE_MARKER_END] : history[TOTAL_FRAMES] );

    if( total_frames_ != history_frames_ )
    {
        gint current_frame_ = info->status_tokens[FRAME_NUM];

        char *time = format_time( total_frames_,(double) info->el.fps );
        if( pm == MODE_STREAM )
        {
            gtk_spin_button_set_value( GTK_SPIN_BUTTON( widget_cache[WIDGET_STREAM_LENGTH] ), (gdouble) info->status_tokens[SAMPLE_MARKER_END]);

            gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_STREAM_LENGTH_LABEL] ), time );
        }

        if( pm != MODE_SAMPLE) {
            update_spin_range2( widget_cache[WIDGET_BUTTON_FADEDUR],0, total_frames_, ( total_frames_ > 25 ? 25 : total_frames_-1 ) );
        }

        update_label_i2( widget_cache[WIDGET_LABEL_TOTFRAMES], total_frames_, 1 );
        
        if( pm == MODE_SAMPLE ) {
            gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_SAMPLELENGTH] ), time );
        }
        
        update_label_i2( widget_cache[ WIDGET_LABEL_TOTFRAMES ], total_frames_, 1 );

        gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_TOTALTIME] ), time );

        if(pm == MODE_SAMPLE)
            gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_SAMPLE_LENGTH_LABEL] ), time );
        else
            gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_SAMPLE_LENGTH_LABEL] ), "0:00:00:00" );

        timeline_set_length( info->tl, (gdouble) total_frames_ , current_frame_);

        if( pm != MODE_STREAM )
            info->uc.reload_hint[HINT_EL] = 1;

        free(time);
    }

    info->status_frame = info->status_tokens[FRAME_NUM];
    
    timeline_set_pos( info->tl, (gdouble) info->status_frame );
    char *current_time_ = format_time( info->status_frame, (double) info->el.fps );
    char *mouse_at_time = format_time( 
            timeline_get_point(TIMELINE_SELECTION(info->tl)),
            (double)info->el.fps);

    update_label_i2(  widget_cache[ WIDGET_LABEL_CURFRAME ],info->status_frame ,1 );

    gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_CURTIME] ), current_time_ );
    gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_MOUSEAT] ), mouse_at_time);


    // WARNING: updating EDL labels when not visible on screen incurs high cost (these labels are updated every frame period)
    if( pm == MODE_SAMPLE && is_edl_displayed() ) {
        gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_SAMPLEPOSITION] ), current_time_ );
        update_label_i2( widget_cache[ WIDGET_LABEL_SAMPLEPOS ], info->status_frame, 1);
    }

    free(current_time_);
    free(mouse_at_time);

    if( last_pm != pm )
        update_status_accessibility( last_pm, pm);

    if( info->status_tokens[FEEDBACK] != history[FEEDBACK] ) {
        if(info->status_tokens[FEEDBACK] == 1) { // when feedback is enabled
            for ( i = 0; fb_widget_map[i].fb_widget_id != FB_WIDGET_NONE; i ++ ) {
               if(gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[fb_widget_map[i].widget_id])))
                   gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[fb_widget_map[i].widget_id]), FALSE);
            }
        }
        else {
            for ( i = 0; fb_widget_map[i].fb_widget_id != FB_WIDGET_NONE; i ++ ) {
               if(!gtk_widget_is_sensitive(GTK_WIDGET(widget_cache[fb_widget_map[i].widget_id])))
                   gtk_widget_set_sensitive(GTK_WIDGET(widget_cache[fb_widget_map[i].widget_id]), TRUE);
            }
        }

        if(info->status_tokens[FEEDBACK] != 
                gtk_toggle_button_get_active(  GTK_TOGGLE_BUTTON( widget_cache[WIDGET_FEEDBACKBUTTON] ) )) {
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_FEEDBACKBUTTON] ), info->status_tokens[FEEDBACK] );
        }
    }

    if( info->status_tokens[MACRO] != history[MACRO] )
    {
        switch(info->status_tokens[MACRO])
        {
            case 1:
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_MACRORECORD1] ), TRUE ); 
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_MACRORECORD] ), TRUE ); break;
            case 2:
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_MACROPLAY1] ), TRUE ); 
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_MACROPLAY] ), TRUE ); break;
            default:
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_MACROSTOP1] ), TRUE );
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_MACROSTOP] ), TRUE ); break;
        }
    }

    if( (pm == MODE_SAMPLE || pm == MODE_STREAM ) && info->status_tokens[CURRENT_ENTRY] != history[CURRENT_ENTRY] ) {
        info->uc.selected_chain_entry = info->status_tokens[CURRENT_ENTRY];
        select_chain_entry(info->uc.selected_chain_entry);
        info->uc.reload_hint[HINT_KF] = 1;
        info->uc.reload_hint[HINT_ENTRY] = 1;
    }

    if( info->status_tokens[CURRENT_ID] != history[CURRENT_ID] || last_pm != pm )
    {
        info->uc.reload_hint[HINT_ENTRY] = 1;
        info->uc.reload_hint[HINT_CHAIN] = 1;
	    info->uc.reload_hint[HINT_MACRO] = 1;

        if( pm != MODE_STREAM )
            info->uc.reload_hint[HINT_EL] = 1;
        if( pm != MODE_PLAIN )
            info->uc.reload_hint[HINT_KF] = 1;

        if( pm == MODE_SAMPLE )
            timeline_set_selection( info->tl, TRUE );
        else {
            timeline_set_selection( info->tl, FALSE );
            timeline_clear_points( info->tl );
        }

        if( pm == MODE_SAMPLE ) {
            info->selection[0] = info->status_tokens[SAMPLE_MARKER_START];
            info->selection[1] = info->status_tokens[SAMPLE_MARKER_END];

            gtk_spin_button_set_value( GTK_SPIN_BUTTON( widget_cache[WIDGET_BUTTON_EL_SELSTART] ), (gdouble) info->selection[0]);
            gtk_spin_button_set_value( GTK_SPIN_BUTTON( widget_cache[WIDGET_BUTTON_EL_SELEND ] ), (gdouble) info->selection[1]);
        }
        if( pm == MODE_STREAM ) {
            info->selection[0] = 0;
            info->selection[1] = total_frames_;
        }
        
        select_slot( info->status_tokens[PLAY_MODE] );
    }

    if( info->status_tokens[TOTAL_SLOTS] != history[TOTAL_SLOTS] && samplebank_ready_)
    {
        int n_samples = 0;
        int n_streams = 0;

        if( pm == MODE_PLAIN || pm == MODE_SAMPLE ) {
            n_samples = info->status_tokens[SAMPLE_COUNT];
            n_streams = info->status_tokens[SAMPLE_INV_COUNT];
        }
        else {
            n_streams = info->status_tokens[SAMPLE_COUNT];
            n_samples = info->status_tokens[SAMPLE_INV_COUNT];
        }

        if( ( (info->uc.real_num_samples > 0 && n_samples > info->uc.real_num_samples ) ||
            (info->uc.real_num_streams > 0 && n_streams > info->uc.real_num_streams)) )
        {
            info->uc.reload_hint[HINT_SLIST] = 1;
        }
        else {
            info->uc.reload_hint[HINT_SLIST] = 2;
        }

        info->uc.real_num_samples = n_samples;
        info->uc.real_num_streams = n_streams;
    }

    if( info->status_tokens[SAMPLE_LOOP_STAT_STOP] != history[SAMPLE_LOOP_STAT_STOP] ) {
	    update_label_i2( widget_cache[WIDGET_LABEL_LOOP_STAT_STOP], info->status_tokens[SAMPLE_LOOP_STAT_STOP],0);
        if( pm == MODE_SAMPLE )
            gtk_spin_button_set_value( GTK_SPIN_BUTTON( widget_cache[ WIDGET_SAMPLE_LOOPSTOP ] ), (gdouble) info->status_tokens[SAMPLE_LOOP_STAT_STOP] );
         if( pm == MODE_STREAM )
             gtk_spin_button_set_value( GTK_SPIN_BUTTON( widget_cache[ WIDGET_STREAM_LOOPSTOP ] ), (gdouble) info->status_tokens[SAMPLE_LOOP_STAT_STOP] );
    }

    if( info->status_tokens[SAMPLE_LOOP_STAT ] != history[SAMPLE_LOOP_STAT] ) {
	    update_label_i2( widget_cache[ WIDGET_LABEL_LOOP_STATS ], info->status_tokens[SAMPLE_LOOP_STAT], 0);
    }

    if( info->status_tokens[SEQ_ACT] != history[SEQ_ACT] )
    {
        info->uc.reload_hint[HINT_SEQ_ACT] = 1;
    }
    if( info->status_tokens[SEQ_CUR] != history[SEQ_CUR] && pm != MODE_PLAIN )
    {
        int in = info->status_tokens[SEQ_CUR];
        if( in < MAX_SEQUENCES )
        {
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_SEQACTIVE]), TRUE );
            indicate_sequence( FALSE, info->sequencer_view->gui_slot[ info->sequence_playing ] );
            info->sequence_playing = in;
            indicate_sequence( TRUE, info->sequencer_view->gui_slot[ info->sequence_playing ] );
        }
        else
        {
            indicate_sequence( FALSE, info->sequencer_view->gui_slot[ info->sequence_playing ] );
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_SEQACTIVE]), FALSE );
        }
    }

    if( history[CURRENT_ID] != info->status_tokens[CURRENT_ID] )
    {
        if(pm == MODE_SAMPLE || pm == MODE_STREAM)
            update_label_i2( widget_cache[ WIDGET_LABEL_CURRENTID], info->status_tokens[CURRENT_ID] ,0);
    }

    if( history[STREAM_RECORDING] != info->status_tokens[STREAM_RECORDING] )
    {
        if(pm == MODE_SAMPLE || pm == MODE_STREAM)
        {
            if( history[CURRENT_ID] == info->status_tokens[CURRENT_ID] )
                info->uc.reload_hint[HINT_RECORDING] = 1;
            if( info->status_tokens[STREAM_RECORDING])
                vj_msg(VEEJAY_MSG_INFO, "Veejay is recording");
            else
                vj_msg(VEEJAY_MSG_INFO, "Recording has stopped");
        }
    }

    if( pm == MODE_PLAIN )
    {
        if( history[SAMPLE_SPEED] != info->status_tokens[SAMPLE_SPEED] )
        {
            int plainspeed =  info->status_tokens[SAMPLE_SPEED];

            update_slider_value2( widget_cache[WIDGET_SPEED_SLIDER], plainspeed, 0);
            if( plainspeed < 0 )
                info->play_direction = -1;
            else
                info->play_direction = 1;
            
            if( plainspeed < 0 ) plainspeed *= -1;

            if( plainspeed == 0 )
            {
                gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_PLAYHINT] ), "Paused" );
            }
            else
            {
                gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_PLAYHINT] ), "Playing" );
            }
        }
    }

    if( pm == MODE_STREAM )
    {
        if( info->status_tokens[STREAM_TYPE] == STREAM_VIDEO4LINUX )
        {
            if(info->uc.cali_duration > 0 )
            {
                GtkWidget *tb = widget_cache[WIDGET_CALI_TAKE_BUTTON];
                info->uc.cali_duration--;
                vj_msg(VEEJAY_MSG_INFO,
                       "Calibrate step %d of %d",
                       info->uc.cali_duration,
                       info->uc.cali_stage);
                if(info->uc.cali_duration == 0)
                {
                    info->uc.cali_stage ++; //@ cali_stage = 1, done capturing black frames

                    switch(info->uc.cali_stage)
                    {
                        case 1: //@ capturing black frames
                            gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_CURRENT_STEP_LABEL] ),
                                "Please take an image of a uniformly lit area in placed in front of your lens.");
                            gtk_button_set_label( GTK_BUTTON(tb), "Take White Frames");
                            break;
                        case 2:
                        case 3:
                            gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_CURRENT_STEP_LABEL] ),
                                "Image calibrated. You may need to adjust brightness.");
                            if(!gtk_widget_is_sensitive(widget_cache[ WIDGET_CALI_SAVE_BUTTON ] ))
                                gtk_widget_set_sensitive(widget_cache[ WIDGET_CALI_SAVE_BUTTON ], TRUE);
                            break;
                        default:
                            gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_CURRENT_STEP_LABEL] ),
                                "Image calibrated. You may need to adjust brightness.");
                            gtk_button_set_label( GTK_BUTTON(tb), "Take Black Frames");
                            veejay_msg(VEEJAY_MSG_ERROR, "Warning, mem leak if not reset first.");
                            break;
                    }

                    if(info->uc.cali_stage >= 2 )
                    {
                        info->uc.cali_stage = 0;
                    }
                }
            }
        }
    }

    update_current_slot(history, pm, last_pm);
}

static void disable_fx_entry() {
    int i;
    gint min=0,max=1,value=0;

    if(gtk_widget_is_sensitive( widget_cache[WIDGET_FRAME_FXTREE2 ] ) ) {
        gtk_widget_set_sensitive_( widget_cache[WIDGET_FRAME_FXTREE2 ] , FALSE );
    }

    gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_EFFECTNAME] ), "");
    gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_EFFECTANIM_NAME] ), "");
    gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_VALUE_FRIENDLYNAME ] ), FX_PARAMETER_VALUE_DEFAULT_HINT );

    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON( widget_cache[WIDGET_BUTTON_ENTRY_TOGGLE] ) ))
    {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_BUTTON_ENTRY_TOGGLE] ), FALSE );
    }

    if( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON( widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE] ) )) 
    {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE] ), FALSE);
    }

    if( (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON( widget_cache[WIDGET_TRANSITION_LOOP] ) ) != 0 ) {
        gtk_spin_button_set_value( GTK_SPIN_BUTTON( widget_cache[WIDGET_TRANSITION_LOOP] ), (gdouble) 0);
    }

    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_TRANSITION_ENABLED] ) ) ) {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_TRANSITION_ENABLED] ), FALSE );
    }
  
    for( i = WIDGET_FX_M1; i <= WIDGET_FX_M4; i ++ ) { 
        if( gtk_widget_is_sensitive(widget_cache[i]) ) {
            gtk_widget_set_sensitive_(widget_cache[i], FALSE);
        }
    }

    GtkWidget *kf_param = widget_cache[ WIDGET_COMBO_CURVE_FX_PARAM ];
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(kf_param));

    for( i = 0; i < MAX_UI_PARAMETERS; i ++ )
    {
        update_slider_range2( widget_cache[WIDGET_SLIDER_P0 + i], min,max, value, 0 );
        gtk_widget_set_sensitive_( widget_cache[WIDGET_SLIDER_BOX_P0 + i], FALSE );
        gtk_widget_set_tooltip_text( widget_cache[WIDGET_SLIDER_P0 + i], "" );

        gtk_label_set_text(GTK_LABEL(widget_cache[WIDGET_LABEL_P0 +i ]), "");

        if( faster_ui_ )
            gtk_widget_hide( widget_cache[WIDGET_FRAME_P0 + i]);
    }
}

static void enable_fx_entry() {

    int *entry_tokens = &(info->uc.entry_tokens[0]);
    int i;

    if(!gtk_widget_is_sensitive( widget_cache[ WIDGET_FRAME_FXTREE2 ] )) {
        gtk_widget_set_sensitive( widget_cache[ WIDGET_FRAME_FXTREE2 ] ,TRUE );
    }

    char *fx_name =  _effect_get_description( entry_tokens[ENTRY_FXID] );

    gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_EFFECTNAME] ),fx_name );
    gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_EFFECTANIM_NAME] ), fx_name );

    if( entry_tokens[ENTRY_VIDEO_ENABLED] != gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_BUTTON_ENTRY_TOGGLE])) ) {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_BUTTON_ENTRY_TOGGLE]), entry_tokens[ENTRY_VIDEO_ENABLED]);
    }
    if( entry_tokens[ENTRY_SUBRENDER_ENTRY] != gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE])) ) {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE]), entry_tokens[ENTRY_SUBRENDER_ENTRY]);
    }

    if( (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_BUTTON_FX_ENTRY])) != info->uc.selected_chain_entry ) {
        gtk_spin_button_set_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_BUTTON_FX_ENTRY]), info->uc.selected_chain_entry );
    }
    if( (int) gtk_spin_button_get_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_TRANSITION_LOOP])) != entry_tokens[ENTRY_TRANSITION_LOOP] ) {
        gtk_spin_button_set_value( GTK_SPIN_BUTTON(widget_cache[WIDGET_TRANSITION_LOOP]), entry_tokens[ENTRY_TRANSITION_LOOP] );
    }

    if( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_TRANSITION_ENABLED]) ) != entry_tokens[ENTRY_TRANSITION_ENABLED]) {
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_TRANSITION_ENABLED]), entry_tokens[ENTRY_TRANSITION_ENABLED]);
    }
  
    for( i = WIDGET_FX_M1; i <= WIDGET_FX_M4; i ++ ) { 
        if( !gtk_widget_is_sensitive(widget_cache[i]) ) {
            gtk_widget_set_sensitive_(widget_cache[i], TRUE);
        }
    }

    if( _effect_get_mix( entry_tokens[ENTRY_FXID] ) )
    {
        if(!gtk_widget_is_sensitive(widget_cache[WIDGET_TRANSITION_ENABLED])) 
            gtk_widget_set_sensitive_(widget_cache[WIDGET_TRANSITION_ENABLED], TRUE);
        if(!gtk_widget_is_sensitive(widget_cache[WIDGET_TRANSITION_LOOP]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_TRANSITION_LOOP], TRUE);
        if(!gtk_widget_is_sensitive(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE], TRUE );
    }
    else {

        if( gtk_widget_is_sensitive(widget_cache[WIDGET_FX_M4]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_FX_M4], FALSE );
        if( gtk_widget_is_sensitive(widget_cache[WIDGET_FX_M3]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_FX_M3], FALSE );
        if( gtk_widget_is_sensitive(widget_cache[WIDGET_TRANSITION_LOOP]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_TRANSITION_LOOP], FALSE );
        if( gtk_widget_is_sensitive(widget_cache[WIDGET_TRANSITION_ENABLED]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_TRANSITION_ENABLED], FALSE );
        if( gtk_widget_is_sensitive(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_SUBRENDER_ENTRY_TOGGLE], FALSE );
    }

    GtkWidget *kf_param = widget_cache[ WIDGET_COMBO_CURVE_FX_PARAM ];
    
    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(kf_param));
    //~ gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kf_param), FX_PARAMETER_DEFAULT_NAME);
    //~ gtk_combo_box_set_active (GTK_COMBO_BOX(kf_param),0);

    int np = _effect_get_np( entry_tokens[ENTRY_FXID] );
    gint min,max,value;

    char kf_param_text[20];

    for( i = 0; i < np ; i ++ )
    {
        if(!gtk_widget_is_sensitive(widget_cache[WIDGET_SLIDER_BOX_P0 + i]))
            gtk_widget_set_sensitive_(widget_cache[WIDGET_SLIDER_BOX_P0 + i], TRUE );

        if( faster_ui_ ){
            gtk_widget_show(widget_cache[WIDGET_FRAME_P0 + i]);
        }

        gchar *tt1 = _effect_get_param_description(entry_tokens[ENTRY_FXID],i);
        gtk_widget_set_tooltip_text( widget_cache[WIDGET_SLIDER_P0 + i], tt1 );
        gtk_label_set_text( GTK_LABEL( widget_cache[WIDGET_LABEL_P0 + i ] ), tt1 );

        if (tt1 != NULL && tt1[0] != '\0' ) {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kf_param), tt1);
        } 
        else 
        { 
            //Fall back if parameter name is missing
            snprintf(kf_param_text,sizeof(kf_param_text), "p%d",i);
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(kf_param), kf_param_text);
        }

        /* WARNING : conditional loop break. Do not add code after this statement. */
        value = entry_tokens[ENTRY_PARAMSET + i];
        if( _effect_get_minmax( entry_tokens[ENTRY_FXID], &min,&max, i ))
        {
            GtkAdjustment *a = gtk_range_get_adjustment( GTK_RANGE( widget_cache[WIDGET_SLIDER_P0 + i] ));
            if( (gint) gtk_adjustment_get_lower(a) == min &&
                (gint) gtk_adjustment_get_upper(a) == max &&
                (gint) gtk_adjustment_get_value(a) == value )
                continue;
    
            update_slider_range2( widget_cache[WIDGET_SLIDER_P0 + i],min,max, value, 0);
        }
    }

    if (np){
        guint signal_id=g_signal_lookup("changed", GTK_TYPE_COMBO_BOX);
        gulong handler_id=handler_id=g_signal_handler_find( (gpointer)kf_param,
                                                            G_SIGNAL_MATCH_ID,
                                                            signal_id,
                                                            0, NULL, NULL, NULL );

        if (handler_id){
            g_signal_handler_block((gpointer)kf_param, handler_id);
        }
        gtk_combo_box_set_active (GTK_COMBO_BOX(kf_param),0);

        if (handler_id)
            g_signal_handler_unblock((gpointer)kf_param, handler_id);


    }

    min = 0; max = 1; value = 0; 
    for( i = np; i < MAX_UI_PARAMETERS; i ++ )
    {
        if( !gtk_widget_is_sensitive(widget_cache[WIDGET_SLIDER_P0 + i]) )
            continue;

        update_slider_range2( widget_cache[WIDGET_SLIDER_P0 + i ],min,max, value, 0 );

        gtk_widget_set_sensitive(widget_cache[ WIDGET_SLIDER_BOX_P0 + i ], FALSE );

        if( faster_ui_ )
          gtk_widget_hide( widget_cache[ WIDGET_FRAME_P0 + i ] );

        gtk_widget_set_tooltip_text( widget_cache[ WIDGET_SLIDER_P0 + i ], "" );

        gtk_label_set_text(GTK_LABEL (widget_cache[WIDGET_LABEL_P0 + i]), "");
    }
}
static void process_reload_hints(int *history, int pm)
{
    int *entry_tokens = &(info->uc.entry_tokens[0]);
    int md = info->uc.reload_hint[HINT_MACRODELAY];
    if( md ) {
        md = md - 1;
        if( md <= 0 ) {
            multi_vims( VIMS_MACRO, "%d", 1 );
		    info->uc.reload_hint[HINT_MACRO] = 1;
            vj_msg(VEEJAY_MSG_INFO, "Started macro record");
        }
    }

    if( pm == MODE_STREAM )
    {
        if(info->uc.reload_hint[HINT_V4L])
            load_v4l_info();

        if( info->uc.reload_hint[HINT_RGBSOLID])
            update_colorselection();
    }

    if( info->uc.reload_hint[HINT_EL] )
    {
        load_editlist_info();
        reload_editlist_contents();
    }

    if( info->uc.reload_hint[HINT_SLIST] )
    {
        gboolean reload_sl = FALSE;
        if( info->uc.reload_hint[HINT_SLIST] == 2 ) {
            info->uc.expected_num_samples = -1;
            info->uc.expected_num_streams = -1;
            reload_sl = TRUE;
        }

        load_samplelist_info( reload_sl );
    }

    if( info->uc.reload_hint[HINT_SEQ_ACT] == 1 )
    {
        load_sequence_list();
    }

    if( info->uc.reload_hint[HINT_RECORDING] == 1 && pm != MODE_PLAIN)
    {
        if(info->status_tokens[STREAM_RECORDING])
        {
            if(!info->uc.recording[pm]) init_recorder( info->status_tokens[STREAM_DURATION], pm );
        }
    }

    if(info->uc.reload_hint[HINT_BUNDLES] == 1 )
        reload_bundles();

    if( info->selected_slot && info->selected_slot->sample_id == info->status_tokens[CURRENT_ID] &&
            info->selected_slot->sample_type == 0 )
    {
        if( history[SAMPLE_FX] != info->status_tokens[SAMPLE_FX])
        {
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CURVE_CHAIN_TOGGLECHAIN]), info->status_tokens[SAMPLE_FX] );

            //also for stream (index is equivalent)
            if(pm == MODE_SAMPLE)
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CHECK_SAMPLEFX]), info->status_tokens[SAMPLE_FX] );
            if(pm == MODE_STREAM)
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(widget_cache[WIDGET_CHECK_STREAMFX]), info->status_tokens[SAMPLE_FX] );
        }
    }

    if( info->uc.reload_hint[HINT_CHAIN] == 1)
    {
        load_effectchain_info();
    }

    info->parameter_lock = 1;
    if(info->uc.reload_hint[HINT_ENTRY] == 1)
    {
        int lpi = load_parameter_info();
        if( lpi == 0 ) {
            if( entry_tokens[ENTRY_FXID] == 0 && gtk_widget_is_sensitive(widget_cache[WIDGET_FRAME_FXTREE2])) {
                disable_fx_entry();
                info->uc.reload_hint[HINT_KF] = 1;
            }
        } else if (lpi == 1 ) {
            if (entry_tokens[ENTRY_FXID] != 0 ) {
                enable_fx_entry();
                info->uc.reload_hint[HINT_KF] = 1;
            }
        }
    }
    info->parameter_lock = 0;

    if( info->uc.reload_hint[HINT_GENERATOR])
    {
        load_generator_info();
    }

    if ( info->uc.reload_hint[HINT_KF]) {
        info->uc.reload_hint_checksums[HINT_KF] = -1;
        vj_kf_refresh(FALSE);
    }

    if( beta__ && info->uc.reload_hint[HINT_HISTORY]) {
        reload_srt();
    }

	if( info->uc.reload_hint[HINT_MACRO]) {
		reload_macros();
	}

	memset( info->uc.reload_hint, 0, sizeof(info->uc.reload_hint ));

    if( md > 0 ) {
        info->uc.reload_hint[HINT_MACRODELAY] = md;
    }
    /*if(!samplebank_ready_) {
        info->uc.reload_hint[HINT_SLIST] = 2;
    }*/
}

void update_gui()
{
    int pm = info->status_tokens[PLAY_MODE];
    int last_pm = info->prev_mode;

    int *history = NULL;

    if( last_pm < 0 )
        history = info->history_tokens[0];
    else
        history = info->history_tokens[ last_pm ];

    if( info->uc.randplayer && pm != last_pm )
    {
        info->uc.randplayer = 0;
        gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_SAMPLERAND] ), FALSE );
    }

    if( pm == MODE_PATTERN && last_pm != pm)
    {
        if(!info->uc.randplayer )
        {
            info->uc.randplayer = 1;
            gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( widget_cache[WIDGET_SAMPLERAND] ), TRUE );
        }
        info->status_tokens[PLAY_MODE] = MODE_SAMPLE;
        pm = MODE_SAMPLE;
    }

    update_globalinfo(history, pm, last_pm);

//  #include <valgrind/callgrind.h>
//    CALLGRIND_START_INSTRUMENTATION;
//    CALLGRIND_TOGGLE_COLLECT;

    process_reload_hints(history, pm);
//   CALLGRIND_TOGGLE_COLLECT;
//    CALLGRIND_STOP_INSTRUMENTATION;
//    CALLGRIND_DUMP_STATS;


    on_vims_messenger();

    update_cpumeter_timeout(NULL);
    update_cachemeter_timeout(NULL);
}

void vj_gui_free()
{
    if(info)
    {
        int i;
        if(info->client)
            vj_client_free(info->client);

        for( i = 0; i < 4;  i ++ )
        {
            if(info->history_tokens[i])
                free(info->history_tokens[i]);
        }
        free(info);
    }
    info = NULL;

    vpf( fx_list_ );
    vpf( bankport_ );
}

void vj_gui_style_setup()
{
    if(!info) return;
    info->color_map = gdk_screen_get_system_visual (gdk_screen_get_default ());
}

void vj_gui_set_debug_level(int level, int n_tracks, int pw, int ph )
{
    veejay_set_debug_level( level );

    vims_verbosity = level;

    if( !mt_set_max_tracks(n_tracks) ) {
        mt_set_max_tracks(5);
    }
}

int vj_gui_get_preview_priority(void)
{
    return 1;
}

void default_bank_values(int *col, int *row )
{
    int nsc = SAMPLEBANK_COLUMNS;
    int nsy = SAMPLEBANK_ROWS;

    if( *col == 0 && *row == 0 )
    {
        SAMPLEBANK_COLUMNS = nsc;
        SAMPLEBANK_ROWS = nsy;
    }
    else
    {
        SAMPLEBANK_ROWS = *row;
        SAMPLEBANK_COLUMNS = *col;
    }
    NUM_SAMPLES_PER_PAGE = SAMPLEBANK_COLUMNS * SAMPLEBANK_ROWS;
    NUM_BANKS = (4096 / NUM_SAMPLES_PER_PAGE );

    veejay_msg(VEEJAY_MSG_INFO, "Sample bank layout is %d rows by %d columns", SAMPLEBANK_ROWS,SAMPLEBANK_COLUMNS );
}

int vj_gui_sleep_time( void )
{
    float f =  (float) info->status_tokens[ELAPSED_TIME];
    float t =  info->el.fps;

    if( t <= 0.0 || t>= 200.0 )
        t = 25.0;
    float n = (1.0 / t) * 1000.0f;

    if( f < n )
        return (int)( n - f );
    return (int) n;
}

int vj_img_cb(GdkPixbuf *img )
{
    int i;
    if( !info->selected_slot || !info->selected_gui_slot || info->status_tokens[PLAY_MODE] == MODE_PLAIN)    {
        return 0;
    }

    int sample_id = info->status_tokens[ CURRENT_ID ];
    int sample_type = info->status_tokens[ PLAY_MODE ];

    if( info->selected_slot->sample_type != sample_type || info->selected_slot->sample_id != sample_id )     {
        return 0;
    }

    if( sample_type == MODE_SAMPLE || sample_type == MODE_STREAM )
    {
        sample_slot_t *slot = find_slot_by_sample( sample_id, sample_type );
        sample_gui_slot_t *gui_slot = find_gui_slot_by_sample( sample_id, sample_type );

        if( slot && gui_slot )
        {
            slot->pixbuf = vj_gdk_pixbuf_scale_simple(img,
                info->image_dimensions[0],info->image_dimensions[1], GDK_INTERP_NEAREST);
            if(slot->pixbuf) {
                gtk_image_set_from_pixbuf_( GTK_IMAGE( gui_slot->image ), slot->pixbuf );
                g_object_unref( slot->pixbuf );
                slot->pixbuf = NULL;
            }
        }
    }
    
    for( i = 0; i < info->sequence_view->envelope_size; i ++ )
    {
        sequence_gui_slot_t *g = info->sequence_view->gui_slot[i];
        if(g->sample_id == info->selected_slot->sample_id && g->sample_type == info->selected_slot->sample_type)
        {
            g->pixbuf_ref = vj_gdk_pixbuf_scale_simple(img,
                                                       info->sequence_view->w,
                                                       info->sequence_view->h,
                                                       GDK_INTERP_NEAREST );
            if( g->pixbuf_ref)
            {
                gtk_image_set_from_pixbuf_( GTK_IMAGE( g->image ), g->pixbuf_ref );
                g_object_unref( g->pixbuf_ref );
                g->pixbuf_ref = NULL;
            }
        }
    }
    return 1;
}

void vj_gui_cb(int state, char *hostname, int port_num)
{
    info->watch.state = STATE_RECONNECT;
    put_text( "entry_hostname", hostname );
    update_spin_value( "button_portnum", port_num );

    //@ clear status
    int i;
    for( i = 0; i < 4; i ++ ) {
        int *h = info->history_tokens[i];
        veejay_memset( h, 0, sizeof(int) * STATUS_TOKENS );
    }
}

static void reloaded_sighandler(int x)
{
    veejay_msg(VEEJAY_MSG_WARNING, "Caught signal %x", x);

    if( x == SIGPIPE ) {
        reloaded_schedule_restart();
    }
    else if  ( x == SIGINT || x == SIGABRT  ) {
        veejay_msg(VEEJAY_MSG_WARNING, "Stopping reloaded");
        exit(0);
    } else if ( x == SIGSEGV ) {
        veejay_msg(VEEJAY_MSG_ERROR, "Found Gremlins in your system.");
        veejay_msg(VEEJAY_MSG_WARNING, "No fresh ale found in the fridge.");
        veejay_msg(VEEJAY_MSG_INFO, "Running with sub-atomic precision...");
        veejay_msg(VEEJAY_MSG_ERROR, "Bugs compromised the system.");
        exit(0);
    }
}

static void veejay_backtrace_handler(int n , siginfo_t *si, void *ptr)
{
    switch(n) {
        case SIGSEGV:
            veejay_msg(VEEJAY_MSG_ERROR,"Found Gremlins in your system."); //@ Suggested by Matthijs
            veejay_msg(VEEJAY_MSG_WARNING, "No fresh ale found in the fridge."); //@
            veejay_msg(VEEJAY_MSG_INFO, "Running with sub-atomic precision..."); //@

            veejay_print_backtrace();
            break;
        default:
            veejay_print_backtrace();
            break;
    }

    //@ Bye
    veejay_msg(VEEJAY_MSG_ERROR, "Bugs compromised the system.");
    exit(EX_SOFTWARE);
}

static void sigsegfault_handler(void) {
    struct sigaction sigst;
    sigst.sa_sigaction = veejay_backtrace_handler;
    sigemptyset(&sigst.sa_mask);
    sigaddset(&sigst.sa_mask, SIGSEGV );
    sigst.sa_flags = SA_SIGINFO | SA_ONESHOT;
    if( sigaction(SIGSEGV, &sigst, NULL) == - 1 )
        veejay_msg(VEEJAY_MSG_ERROR,"%s", strerror(errno));
}

void register_signals()
{
    signal( SIGINT,  reloaded_sighandler );
    signal( SIGPIPE, reloaded_sighandler );
    signal( SIGQUIT, reloaded_sighandler );
//  signal( SIGSEGV, reloaded_sighandler );
    signal( SIGABRT, reloaded_sighandler );

    sigsegfault_handler();
}

void vj_gui_wipe()
{
    int i;
    veejay_memset( info->status_tokens, 0, sizeof(int) * STATUS_TOKENS );
    veejay_memset( info->uc.entry_tokens,0, sizeof(int) * ENTRY_LAST);
    for( i = 0 ; i < 4; i ++ )
    {
        veejay_memset(info->history_tokens[i],0, sizeof(int) * (STATUS_TOKENS+1));
    }

    reset_samplebank();
}

GtkWidget *new_bank_pad(GtkWidget *box)
{
    GtkWidget *pad = gtk_notebook_new();
    gtk_notebook_set_tab_pos( GTK_NOTEBOOK(pad), GTK_POS_BOTTOM );
    gtk_notebook_set_show_tabs( GTK_NOTEBOOK(pad ), FALSE );
    gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET(pad), TRUE, TRUE, 0);
    return pad;
}

gboolean slider_scroll_event( GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
    gint i = GPOINTER_TO_INT(user_data);
    if(ev->direction == GDK_SCROLL_UP ) {
        PARAM_CHANGED( i, 1, slider_names_[i].text );
    } else if (ev->direction == GDK_SCROLL_DOWN ) {
        PARAM_CHANGED( i, -1, slider_names_[i].text );
    }
    return FALSE;
}

gboolean speed_scroll_event( GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
    int plainspeed =  info->status_tokens[SAMPLE_SPEED];
    if(ev->direction == GDK_SCROLL_UP ) {
        plainspeed = plainspeed + 1;
    } else if (ev->direction == GDK_SCROLL_DOWN ) {
        plainspeed = plainspeed - 1;
    }
    update_slider_value( "speed_slider", plainspeed, 0 );
    return FALSE;
}

gboolean slow_scroll_event( GtkWidget *widget, GdkEventScroll *ev, gpointer user_data)
{
    int plainspeed =  get_slider_val("slow_slider");
    if(ev->direction == GDK_SCROLL_DOWN ) {
        plainspeed = plainspeed - 1;
    } else if (ev->direction == GDK_SCROLL_UP ) {
        plainspeed = plainspeed + 1;
    }
    if(plainspeed < 1 )
        plainspeed = 1;
    update_slider_value("slow_slider",plainspeed,0);
    vj_msg(VEEJAY_MSG_INFO, "Slow video to %2.2f fps",
        info->el.fps / (float) plainspeed );
    return FALSE;
}

void vj_gui_set_geom( int x, int y )
{
    geo_pos_[0] = x;
    geo_pos_[1] = y;
}

void vj_event_list_free()
{
    int i;
    for( i = 0; i < VIMS_MAX; i ++ ) {
        if( vj_event_list[i].format )
            free(vj_event_list[i].format);
        if( vj_event_list[i].descr )
            free(vj_event_list[i].descr);
        if( vj_event_list[i].args )
            free(vj_event_list[i].args);
    }

    veejay_memset( vj_event_list, 0, sizeof(vj_event_list));
}

char reloaded_css_file[1024];
int  use_css_file = 0;
gboolean smallest_possible = FALSE;

void vj_gui_set_stylesheet(const char *css_file, gboolean small_as_possible) {
    smallest_possible = small_as_possible;

    if( css_file == NULL ) {
        veejay_msg(VEEJAY_MSG_DEBUG,"Using system's default style");
        use_css_file = 0;
        return;
    }

    if(strlen(css_file)==7) {
        if(strncasecmp(css_file, "default",7) == 0 ) {
            veejay_msg(VEEJAY_MSG_DEBUG, "Using reloaded's default style");
            snprintf( reloaded_css_file, sizeof(reloaded_css_file), "%s/%s", RELOADED_DATADIR, "gveejay.reloaded.css");
            use_css_file = 1;
            return;
        }
    }

    veejay_msg(VEEJAY_MSG_DEBUG, "Using CSS %s", reloaded_css_file);
    snprintf( reloaded_css_file, sizeof(reloaded_css_file), "%s", css_file);
    use_css_file = 1;
}

void vj_gui_activate_stylesheet(vj_gui_t *gui)
{
    GtkCssProvider *css = gtk_css_provider_new();

    gtk_style_context_add_provider_for_screen ( gdk_screen_get_default (),GTK_STYLE_PROVIDER (css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    
    if(use_css_file) {
        GError* error = NULL;
        veejay_msg(VEEJAY_MSG_DEBUG, "Loading stylesheet %s", reloaded_css_file);
        if(!gtk_css_provider_load_from_path(css, reloaded_css_file, &error))
        {
            veejay_msg(VEEJAY_MSG_WARNING, "Could not load CSS file: %s , %s", error->message, reloaded_css_file);
            g_error_free (error);
        }
        GtkStyleContext *sc = gtk_widget_get_style_context( GTK_WIDGET(gui->curve) );
        GdkRGBA bg,col,border;
        GtkStateFlags context_state = gtk_style_context_get_state(sc);
        vj_gtk_context_get_color(sc, "background-color", context_state, &bg);
        vj_gtk_context_get_color(sc, "border-color", context_state, &border);
        gtk_style_context_get_color(sc, context_state, &col );

        gtk3_curve_set_color_background_rgba (GTK_WIDGET(gui->curve), bg.red, bg.green, bg.blue, 1.0);
        gtk3_curve_set_color_curve_rgba (GTK_WIDGET(gui->curve), col.red, col.green, col.blue, 1.0);
        gtk3_curve_set_color_grid_rgba (GTK_WIDGET(gui->curve), col.red, col.green, col.blue, 0.33);
        gtk3_curve_set_color_cpoint_rgba (GTK_WIDGET(gui->curve), border.red, border.green, border.blue, 1.0);

    }
    else {
        gtk3_curve_set_color_background_rgba (GTK_WIDGET(gui->curve), 1.0f,1.0f,1.0f, 1.0);
        gtk3_curve_set_color_curve_rgba (GTK_WIDGET(gui->curve), 0.0f, 0.0f, 0.0f, 1.0);
        gtk3_curve_set_color_grid_rgba (GTK_WIDGET(gui->curve), 0.0f, 0.0f, 0.0f, 0.33);
        gtk3_curve_set_color_cpoint_rgba (GTK_WIDGET(gui->curve), 0.0f, 0.0f, 0.0f, 1.0);

    }
   
#if (GTK_MINOR_VERSION >= 20)
    gchar *css_in_mem = gtk_css_provider_to_string(css);
    gchar *data = NULL;
        
    if( smallest_possible ) {
        data = g_strconcat(css_in_mem, "\n* { font-size:98%; }\nframe,box,scale,spinbutton,button,radiobutton,checkbutton,entry, .vertical { padding-left:0px; padding-right: 0px }\nbutton,checkbutton,radiobutton,spinbutton { padding-top:1px; padding-bottom:1px;}\n", NULL );
    }
    else {
        data = g_strconcat(css_in_mem, "\n* { font-size:98%; }", NULL);
    }

    gtk_css_provider_load_from_data(css, data,-1, NULL);

    g_clear_object(&css);
    g_free(css_in_mem);
    g_free(data);
#endif
}

static int auto_connect_to_veejay(char *host, int port_num)
{
    char *hostname = (host == NULL ? "localhost" : host );
    int i,j;
    
    for( i = port_num; i < 9999; i += 1000 ) {
        //connect client at first available server
        if( vj_gui_reconnect( hostname, NULL, i ) ) {
            info->watch.state = STATE_PLAYING;
            veejay_msg(VEEJAY_MSG_INFO,"Trying to connect to %s:%d", hostname, i);
            multrack_audoadd( info->mt, hostname, i);
            multitrack_set_master_track( info->mt, 0 );
            // set reconnect info
            update_spin_value( "button_portnum", i );
            put_text( "entry_hostname", hostname );

            // setup tracks
            for( j = (i+1000); j < 9999; j+= 1000 )
            {
                veejay_msg(VEEJAY_MSG_INFO, "Trying to add %s:%d as a track", hostname, j);
                multrack_audoadd( info->mt, hostname, j);
            }
            multitrack_set_quality( info->mt, 1 );
            i = j;

            return 1;
        }
    }

    update_spin_value( "button_portnum", port_num );
    put_text( "entry_hostname", hostname );

    return 0;
}

void vj_gui_init(const char *glade_file,
                 int launcher,
                 char *hostname,
                 int port_num,
                 int use_threads,
                 int load_midi,
                 char *midi_file,
                 gboolean beta,
                 gboolean auto_connect,
                 gboolean fasterui)
{
    int i;
    char text[100];

    faster_ui_ = fasterui;

    vj_gui_t *gui = (vj_gui_t*)vj_calloc(sizeof(vj_gui_t));
    if(!gui)
    {
        return;
    }
    snprintf( glade_path, sizeof(glade_path), "%s/%s",RELOADED_DATADIR,glade_file);
    
	veejay_msg(VEEJAY_MSG_DEBUG, "Loading glade file %s", glade_path);

    veejay_memset( gui->status_tokens, 0, sizeof(int) * STATUS_TOKENS );
    veejay_memset( gui->sample, 0, 2 );
    veejay_memset( gui->selection, 0, 3 );
    veejay_memset( &(gui->uc), 0, sizeof(veejay_user_ctrl_t));
    gui->uc.selected_parameter_id = -1;
    veejay_memset( gui->uc.entry_tokens,0, sizeof(int) * ENTRY_LAST);
    gui->prev_mode = -1;
    veejay_memset( &(gui->el), 0, sizeof(veejay_el_t));
    gui->sample_banks = (sample_bank_t**) vj_calloc(sizeof(sample_bank_t*) * NUM_BANKS );

    for( i = 0 ; i < 4; i ++ )
    {
        gui->history_tokens[i] = (int*) vj_calloc(sizeof(int) * (STATUS_TOKENS+1));
    }
    for( i = 0; i < NUM_HINTS ; i ++ ) {
        gui->uc.reload_hint_checksums[i] = -1;
    }

    slider_names_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * MAX_UI_PARAMETERS );
    slider_box_names_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * MAX_UI_PARAMETERS );
    param_frame_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * MAX_UI_PARAMETERS );
    param_names_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * MAX_UI_PARAMETERS );
    param_incs_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * MAX_UI_PARAMETERS );
    param_decs_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * MAX_UI_PARAMETERS );
    gen_decs_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * GENERATOR_PARAMS );
    gen_incs_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * GENERATOR_PARAMS );
    gen_names_ = (widget_name_t*) vj_calloc(sizeof(widget_name_t) * GENERATOR_PARAMS );

    for( i = 0; i < MAX_UI_PARAMETERS; i ++ )
    {
        snprintf(text,sizeof(text), "frame_p%d", i );
        param_frame_[i].text = strdup( text );

        snprintf(text,sizeof(text),"slider_p%d" , i );
        slider_names_[i].text = strdup( text );

        snprintf(text,sizeof(text),"slider_box_p%d" , i );
        slider_box_names_[i].text = strdup( text );

        snprintf(text,sizeof(text),"label_p%d" , i );
        param_names_[i].text = strdup( text );

        snprintf(text,sizeof(text),"inc_p%d", i );
        param_incs_[i].text = strdup( text );

        snprintf(text,sizeof(text), "dec_p%d", i );
        param_decs_[i].text = strdup( text );
    }

    for( i = 0; i < GENERATOR_PARAMS; i ++ )
    {
        snprintf(text,sizeof(text), "slider_g%d",i);
        gen_names_[i].text = strdup( text );

        snprintf(text,sizeof(text), "dec_g%d", i);
        gen_decs_[i].text = strdup(text);

        snprintf(text,sizeof(text), "inc_g%d", i );
        gen_incs_[i].text = strdup(text);
    }

    gui->uc.reload_force_avoid = FALSE;

    veejay_memset( vj_event_list, 0, sizeof(vj_event_list));

    gui->client = NULL;
    GError* error = NULL;
    gui->main_window = gtk_builder_new ();
    if (!gtk_builder_add_from_file (gui->main_window, glade_path, &error))
    {
        free(gui);
        free(gui->main_window);
        veejay_msg(VEEJAY_MSG_ERROR, "Couldn't load builder file: %s , %s", error->message, glade_path);
        g_error_free (error);
        return;
    }
    info = gui;

    for( i = 0; i < MAX_UI_PARAMETERS; i ++ ) {
      add_class_by_name( slider_names_[i].text, "p_slider" );
    }

    add_class_by_name( "speed_slider", "h_slider" );
    add_class_by_name( "slow_slider", "h_slider" );
    add_class_by_name( "framerate", "h_slider" );

    GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );

    init_widget_cache();

    //set "connection" button has default in veejay connection dialog
    gtk_entry_set_activates_default(GTK_ENTRY(glade_xml_get_widget_( info->main_window,
                                                                    "entry_hostname" )),
                                    TRUE);
    gtk_entry_set_activates_default(GTK_ENTRY(glade_xml_get_widget_( info->main_window,
                                                                    "button_portnum" )),
                                    TRUE);
    GtkWidget *vj_button = glade_xml_get_widget_( info->main_window, "button_veejay" );
    gtk_widget_set_can_default(vj_button,TRUE);
    GtkWidget *connection_dial = glade_xml_get_widget_( info->main_window,
                                                       "veejay_connection");
    gtk_window_set_transient_for (GTK_WINDOW(connection_dial),GTK_WINDOW (mainw));
    gtk_window_set_default(GTK_WINDOW(connection_dial), vj_button);

    gtk_builder_connect_signals( gui->main_window , NULL);
    GtkWidget *frame = glade_xml_get_widget_( info->main_window, "markerframe" );
    info->tl = timeline_new();
    add_class(info->tl, "timeline");
    set_tooltip_by_widget(info->tl, tooltips[TOOLTIP_TIMELINE].text );

    g_signal_connect( info->tl, "pos_changed",
        (GCallback) on_timeline_value_changed, NULL );
    g_signal_connect( info->tl, "in_point_changed",
        (GCallback) on_timeline_in_point_changed, NULL );
    g_signal_connect( info->tl, "out_point_changed",
        (GCallback) on_timeline_out_point_changed, NULL );
    g_signal_connect( info->tl, "bind_toggled",
        (GCallback) on_timeline_bind_toggled, NULL );
    g_signal_connect( info->tl, "cleared",
        (GCallback) on_timeline_cleared, NULL );

    bankport_ = vpn( VEVO_ANONYMOUS_PORT );

    gtk_container_add( GTK_CONTAINER(frame), info->tl );
    gtk_widget_show_all(frame);

#ifdef STRICT_CHECKING
    debug_spinboxes();
#endif

    snprintf(text, sizeof(text), "Reloaded - version %s",VERSION);
    gtk_label_set_text(GTK_LABEL(glade_xml_get_widget_(info->main_window,
                                                        "build_revision")),
                       text);

    g_signal_connect( mainw, "destroy",
            G_CALLBACK( gveejay_quit ),
            NULL );
    g_signal_connect( mainw, "delete-event",
            G_CALLBACK( gveejay_quit ),
            NULL );

    GtkWidget *box = glade_xml_get_widget_( info->main_window, "sample_bank_hbox" );
    info->sample_bank_pad = new_bank_pad( box );

    //QuickSelect slots
    create_ref_slots( QUICKSELECT_SLOTS );

    //SEQ
    create_sequencer_slots( SEQUENCER_COL, SEQUENCER_ROW );

    veejay_memset( vj_event_list, 0, sizeof( vj_event_list ));
    veejay_memset( vims_keys_list, 0, sizeof( vims_keys_list) );

    info->elref = NULL;
    info->effect_info = (effect_constr**) vj_calloc(sizeof(effect_constr*) * EFFECT_LIST_SIZE );
    info->devlist = NULL;
    info->chalist = NULL;
    info->editlist = NULL;

    setup_vimslist();
    setup_effectchain_info();
    setup_effectlist_info();
    setup_editlist_info();
    setup_samplelist_info();
    setup_v4l_devices();
    setup_macros();
	setup_colorselection();
    setup_rgbkey();
    setup_bundles();
    setup_server_files();
    setup_generators();

    text_defaults();

    GtkWidget *fgb = glade_xml_get_widget_(info->main_window, "boxtext" );
    GtkWidget *bgb = glade_xml_get_widget_(info->main_window, "boxbg" );
    GtkWidget *rb = glade_xml_get_widget_(info->main_window, "boxred" );
    GtkWidget *gb = glade_xml_get_widget_(info->main_window, "boxgreen" );
    GtkWidget *bb = glade_xml_get_widget_(info->main_window, "boxblue" );
    GtkWidget *lnb = glade_xml_get_widget_(info->main_window,"boxln" );
    g_signal_connect(G_OBJECT( bgb ), "draw",
                     G_CALLBACK( boxbg_draw ), NULL);
    g_signal_connect(G_OBJECT( fgb ), "draw",
                     G_CALLBACK( boxfg_draw ), NULL);
    g_signal_connect(G_OBJECT( lnb ), "draw",
                     G_CALLBACK( boxln_draw ), NULL);
    g_signal_connect(G_OBJECT( rb ), "draw",
                     G_CALLBACK( boxred_draw ), NULL);
    g_signal_connect(G_OBJECT( gb ), "draw",
                     G_CALLBACK( boxgreen_draw ), NULL);
    g_signal_connect(G_OBJECT( bb ), "draw",
                     G_CALLBACK( boxblue_draw ), NULL);

    set_toggle_button( "button_252", vims_verbosity );

    int pw = MAX_PREVIEW_WIDTH;
    int ph = MAX_PREVIEW_HEIGHT;
            
   
    GtkWidget *img_wid = widget_cache[WIDGET_IMAGEA];

    gui->mt = multitrack_new((void(*)(int,char*,int)) vj_gui_cb,
                             NULL,
                             glade_xml_get_widget_( info->main_window,
                                                   "gveejay_window" ),
                             glade_xml_get_widget_( info->main_window,
                                                   "mt_box" ),
                             glade_xml_get_widget_( info->main_window,
                                                   "statusbar") ,
                             glade_xml_get_widget_( info->main_window,
                                                   "previewtoggle"),
                             pw,
                             ph,
                             img_wid,
                             (void*) gui,
                             use_threads);

    GtkWidget *curve_container = widget_cache[ WIDGET_CURVECONTAINER ];
    gui->curve = gtk3_curve_new ();
    add_class(gui->curve, "curve");

    gtk_container_add(GTK_CONTAINER(curve_container), gui->curve);
    gtk_widget_show_all(curve_container);

    veejay_memset( &info->watch, 0, sizeof(watchdog_t));
    info->watch.state = STATE_WAIT_FOR_USER; //

    vj_gui_activate_stylesheet(gui);
    veejay_memset(&(info->watch.p_time),0,sizeof(struct timeval));
    info->midi =  vj_midi_new( info->main_window );
 

    if(!beta) // srt-titling sequence stuff
    {
        GtkWidget *srtpad = gtk_notebook_get_nth_page(GTK_NOTEBOOK( widget_cache[WIDGET_NOTEBOOK18]),4);
        gtk_widget_hide(srtpad);
    }

    beta__ = beta;

    update_spin_range( "spin_framedelay", 1, MAX_SLOW, 0);
    update_spin_range2( widget_cache[WIDGET_SPIN_SAMPLESPEED], -25,25,1);
    update_slider_range( "speed_slider", -25,25,1,0);
    update_slider_range( "slow_slider",1,MAX_SLOW,1,0);

    if( load_midi )
       vj_midi_load(info->midi,midi_file);

    for( i = 0 ; i < MAX_UI_PARAMETERS; i ++ )
    {
        GtkWidget *slider = glade_xml_get_widget_( info->main_window, slider_names_[i].text );
        g_signal_connect( slider, "scroll-event", G_CALLBACK(slider_scroll_event), (gpointer) castIntToGpointer(i) );
        update_slider_range( slider_names_[i].text, 0,1,0,0);
    }

    g_signal_connect(glade_xml_get_widget_(info->main_window, "speed_slider"), "scroll-event",
                     G_CALLBACK(speed_scroll_event), NULL );
    g_signal_connect(glade_xml_get_widget_(info->main_window, "slow_slider"), "scroll-event",
                     G_CALLBACK(slow_scroll_event), NULL );

        char *have_snoop = getenv( "RELOADED_KEY_SNOOP" );
    if( have_snoop == NULL )
    {
        veejay_msg(VEEJAY_MSG_DEBUG, "Use setenv RELOADED_KEY_SNOOP=1 to mirror veejay server keyb layout" );
    }else
    {
        use_key_snoop = atoi(have_snoop);
        if( use_key_snoop < 0 || use_key_snoop > 1 )
            use_key_snoop = 0;
    }

    vj_gui_style_setup();

    GtkWidget *lw = glade_xml_get_widget_( info->main_window, "veejay_connection");

    if( geo_pos_[0] >= 0 && geo_pos_[1] >= 0 )
        gtk_window_move( GTK_WINDOW(lw), geo_pos_[0], geo_pos_[1] );


    if( auto_connect ) {
        if(auto_connect_to_veejay(hostname, port_num)) {
            veejay_stop_connecting(gui);
        }
    }

    if(info->watch.state != STATE_PLAYING) {
        if(hostname) {
            put_text("entry_hostname",hostname);
        }
        update_spin_value( "button_portnum", port_num );

        reloaded_show_launcher ();
    }

    if( user_preview ) {
        set_toggle_button( "previewtoggle", 1 );
    }

    gtk_widget_show( info->sample_bank_pad );
}

void vj_gui_preview(void)
{
    gint w = 0;
    gint h = 0;
    gint tmp_w = info->el.width;
    gint tmp_h = info->el.height;

    multitrack_get_preview_dimensions( tmp_w,tmp_h, &w, &h );

    update_spin_value( "priout_width", w );
    update_spin_value( "priout_height", h );

    update_spin_range( "preview_width", 16, w, w);
    update_spin_range( "preview_height", 16, h, h );

    update_spin_incr( "preview_width", 16, 0 );
    update_spin_incr( "preview_height", 16, 0 );
    update_spin_incr( "priout_width", 16,0 );
    update_spin_incr( "priout_height", 16, 0 );

    info->image_w = w;
    info->image_h = h;

    GdkRectangle result;
    widget_get_rect_in_screen(
        glade_xml_get_widget_(info->main_window, "quickselect"),
        &result
    );
    gdouble ratio = (gdouble) h / (gdouble) w;

    gint image_width = 32;
    gint image_height = 32 *ratio;

    info->sequence_view->w = image_width;
    info->sequence_view->h = image_height;

    for( int i = 0; i < QUICKSELECT_SLOTS; i ++ ) {
        gtk_widget_set_size_request_( info->sequence_view->gui_slot[i]->image, info->sequence_view->w,info->sequence_view->h );
    }
}

void gveejay_preview( int p )
{
    user_preview = p;
}

int gveejay_user_preview()
{
    return user_preview;
}

int vj_gui_reconnect(char *hostname,char *group_name, int port_num)
{
    int k = 0;
    for( k = 0; k < 4; k ++ )
        veejay_memset( info->history_tokens[k] , 0, (sizeof(int) * STATUS_TOKENS) );

    veejay_memset( info->status_tokens, 0, sizeof(int) * STATUS_TOKENS );

    if(!hostname && !group_name )
    {
        veejay_msg(VEEJAY_MSG_ERROR,"Invalid host/group name given");
        return 0;
    }

    if(info->client )
    {
        error_dialog("Warning", "You should disconnect first");
        return 0;
    }

    if(!info->client)
    {
        info->client = vj_client_alloc(0,0,0);
        if(!info->client)
        {
            return 0;
        }
    }

    if(!vj_client_connect( info->client, hostname, group_name, port_num ) )
    {
        if(info->client)
            vj_client_free(info->client);
        info->client = NULL;
        return 0;
    }

    vj_msg(VEEJAY_MSG_INFO,
           "New connection with Veejay running on %s port %d",
           (group_name == NULL ? hostname : group_name), port_num );

    veejay_msg(VEEJAY_MSG_INFO,
               "Connection established with %s:%d (Track 0)",
               hostname,port_num);

    info->status_lock = 1;
    info->parameter_lock = 1;
    info->uc.expected_num_samples = -1;
    info->uc.expected_num_streams = -1;
    info->uc.selected_chain_entry = 0;

    single_vims( VIMS_PROMOTION );

    load_editlist_info();

    update_slider_value( "framerate", info->el.fps,  0 );

    veejay_memset( vims_keys_list, 0 , sizeof(vims_keys_list));
    veejay_memset( vj_event_list,  0, sizeof( vj_event_list));

    load_effectlist_info();
    reload_vimslist();
    reload_editlist_contents();
    reload_bundles();

    set_feedback_status();

    GtkWidget *w = glade_xml_get_widget_(info->main_window, "gveejay_window" );
    gtk_widget_show( w );

    if( geo_pos_[0] >= 0 && geo_pos_[1] >= 0 )
        gtk_window_move(GTK_WINDOW(w), geo_pos_[0], geo_pos_[1] );

    /*  int speed = info->status_tokens[SAMPLE_SPEED];
    if( speed < 0 )
    info->play_direction = -1; else info->play_direction=1;
    if( speed < 0 ) speed *= -1;*/
    update_label_str( "label_hostnamex", (hostname == NULL ? group_name: hostname ) );
    update_label_i( "label_portx",port_num,0);

    info->status_lock = 0;
    info->parameter_lock = 0;

    multitrack_configure(info->mt,
                         info->el.fps,
                         info->el.width,
                         info->el.height,
                         &preview_box_w_,
                         &preview_box_h_ );

    vj_gui_preview();

   // info->uc.reload_hint[HINT_SLIST] = 2;
    info->uc.reload_hint[HINT_CHAIN] = 1;
    info->uc.reload_hint[HINT_ENTRY] = 1;
    info->uc.reload_hint[HINT_SEQ_ACT] = 1;
    info->uc.reload_hint[HINT_HISTORY] = 1;
    info->uc.reload_hint[HINT_KF] = 1;

    // periodically pull information from veejay
    g_timeout_add( 1000, periodic_pull, NULL );
   
    gettimeofday( &(info->time_last) , 0 );
    //g_idle_add_full( G_PRIORITY_LOW,  gveejay_idle,NULL,  NULL );

    return 1;
}

static void veejay_stop_connecting(vj_gui_t *gui)
{
    GtkWidget *veejay_conncection_window;

    if(!gui->sensitive)
        vj_gui_enable();

    info->launch_sensitive = 0;

    veejay_conncection_window = glade_xml_get_widget_(info->main_window, "veejay_connection");
    gtk_widget_hide(veejay_conncection_window);
    GtkWidget *mw = glade_xml_get_widget_(info->main_window,"gveejay_window" );

    gtk_widget_show( mw );
    if( geo_pos_[0] >= 0 && geo_pos_[1] >= 0 )
        gtk_window_move( GTK_WINDOW(mw), geo_pos_[0], geo_pos_[1] );
}

void reloaded_launcher()
{
    info->watch.state = STATE_RECONNECT;
}

void reloaded_show_launcher()
{
    info->watch.state = STATE_WAIT_FOR_USER;
    info->launch_sensitive = TRUE;

    GtkWidget *veejay_connection = glade_xml_get_widget_(info->main_window,"veejay_connection" );
    GtkWidget *mainw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    gtk_window_set_transient_for(GTK_WINDOW(veejay_connection),GTK_WINDOW(mainw));
    gtk_widget_show(veejay_connection);
}

void reloaded_schedule_restart()
{
    info->watch.state = STATE_STOPPED;
}

void reloaded_restart()
{
    GtkWidget *mw = glade_xml_get_widget_(info->main_window,"gveejay_window" );
    // disable and hide mainwindow
    if(info->sensitive)
        vj_gui_disable();
    gtk_widget_hide( mw );

    vj_gui_wipe();

    reloaded_show_launcher();
}

gboolean    is_alive( int *do_sync )
{
    void *data = info;
    vj_gui_t *gui = (vj_gui_t*) data;

    if( gui->watch.state == STATE_STOPPED ) 
    {
       vj_gui_disconnect(TRUE);
       reloaded_restart();
       gui->watch.state = STATE_WAIT_FOR_USER;
       return TRUE; 
    }

    if( gui->watch.state == STATE_PLAYING )
    {
        *do_sync = 1;
        return TRUE;
    }

    if( gui->watch.state == STATE_RECONNECT )
    {
        vj_gui_disconnect(TRUE);
        vj_gui_wipe();
        gui->watch.state = STATE_CONNECT;
        return TRUE;
    }

    if( gui->watch.state == STATE_QUIT )
    {
        if(info->client) 
            vj_gui_disconnect(FALSE);
        vj_gui_wipe();
        return FALSE;
    }

    if( gui->watch.state == STATE_CONNECT )
    {
        char *remote;
        int port;
        remote = get_text( "entry_hostname" );
        port    = get_nums( "button_portnum" );

        veejay_msg(VEEJAY_MSG_INFO, "Connecting to %s: %d", remote,port );
        if(!vj_gui_reconnect( remote, NULL, port ))
        {
            reloaded_schedule_restart();
        }
        else
        {
            info->watch.state = STATE_PLAYING;

            if( use_key_snoop ) {

#ifdef HAVE_SDL
                info->key_id = gtk_key_snooper_install( key_handler , NULL);
#endif
            }
            multrack_audoadd( info->mt, remote, port );
            multitrack_set_master_track( info->mt, 0 );
            multitrack_set_quality( info->mt, 1 );

            *do_sync = 1;

            veejay_stop_connecting(gui);
        }
    }
    return TRUE;
}

void vj_gui_disconnect(int restart_schedule)
{
    if(info->key_id)
        gtk_key_snooper_remove( info->key_id );
    /* reset all trees */
//  reset_tree("tree_effectlist");
//  reset_tree("tree_effectmixlist");

    reset_fxtree();
    reset_tree("tree_chain");
    reset_tree("tree_sources");
    reset_tree("editlisttree");

    if (restart_schedule) {
        reloaded_schedule_restart();
        clear_samplebank_pages();
        free_samplebank();
    }
    else {
        multitrack_close_tracks(info->mt);
        multitrack_disconnect(info->mt);
        free_samplebank();
    }

    if(info->client)
    {
        vj_client_close(info->client);
        vj_client_free(info->client);
        info->client = NULL;
    }

    info->key_id = 0;
}

void vj_gui_disable()
{
    if(gtk_widget_is_sensitive( GTK_WIDGET(widget_cache[WIDGET_VEEJAY_BOX] ))) {
        gtk_widget_set_sensitive( GTK_WIDGET(widget_cache[WIDGET_VEEJAY_BOX] ), FALSE );
    }

    info->sensitive = 0;
}

void vj_gui_enable()
{
    if(!gtk_widget_is_sensitive( GTK_WIDGET(widget_cache[WIDGET_VEEJAY_BOX] ))) {
        gtk_widget_set_sensitive( GTK_WIDGET(widget_cache[WIDGET_VEEJAY_BOX] ), TRUE );
    }

    info->sensitive = 1;
}

static void widget_get_rect_in_screen (GtkWidget *widget, GdkRectangle *r)
{
//GdkRectangle extents;
//GdkWindow *window;
//window = GDK_WINDOW(gtk_widget_get_parent_window(widget)); /* getting parent window */
//gdk_window_get_root_origin(window, &x,&y); /* parent's left-top screen coordinates */
//gdk_drawable_get_size(window, &w,&h); /* parent's width and height */
//gdk_window_get_frame_extents(window, &extents); /* parent's extents (including decorations) */
//r->x = x + (extents.width-w)/2 + widget->allocation.x; /* calculating x (assuming: left border size == right border size) */
//r->y = y + (extents.height-h)-(extents.width-w)/2 + widget->allocation.y; /* calculating y (assuming: left border size == right border size == bottom border size) */
    r->x = 0;
    r->y = 0;
    GtkAllocation all;
    gtk_widget_get_allocation(widget, &all);
    r->width = all.width;
    r->height = all.height;
}

/* --------------------------------------------------------------------------------------------------------------------------
 *  Function that creates the sample-bank initially, just add the widget to the GUI and create references for the
 *  sample_banks-structure so that the widgets are easiely accessable
 *  The GUI componenets are in sample_bank[i]->gui_slot[j]
 *
  -------------------------------------------------------------------------------------------------------------------------- */
static void samplebank_size_allocate(GtkWidget *widget, GtkAllocation *allocation, void *data)
{
    setup_samplebank( 
            SAMPLEBANK_COLUMNS, SAMPLEBANK_ROWS, allocation, &(info->image_dimensions[0]),&(info->image_dimensions[1]) );
  
    if(!samplebank_ready_) {
        samplebank_ready_ = 1;
       info->uc.reload_hint[HINT_SLIST] = 2;
    }
}

/* Add a page to the notebook and initialize slots */
static int add_bank( gint bank_num )
{
    gchar str_label[5];
    gchar frame_label[20];
    sprintf(str_label, "%d", bank_num );
    sprintf(frame_label, "Slots %d to %d",
        (bank_num * NUM_SAMPLES_PER_PAGE), (bank_num * NUM_SAMPLES_PER_PAGE) + NUM_SAMPLES_PER_PAGE  );

    info->sample_banks[bank_num] = (sample_bank_t*) vj_calloc(sizeof(sample_bank_t));
    info->sample_banks[bank_num]->bank_number = bank_num;
    sample_slot_t **slot = (sample_slot_t**) vj_calloc(sizeof(sample_slot_t*) * NUM_SAMPLES_PER_PAGE);
    sample_gui_slot_t **gui_slot = (sample_gui_slot_t**) vj_calloc(sizeof(sample_gui_slot_t*) * NUM_SAMPLES_PER_PAGE );

    int j;
    for(j = 0;j < NUM_SAMPLES_PER_PAGE; j ++ )
    {
        slot[j] = (sample_slot_t*) vj_calloc(sizeof(sample_slot_t) );
        gui_slot[j] = (sample_gui_slot_t*) vj_calloc(sizeof(sample_gui_slot_t));
        slot[j]->slot_number = j;
        slot[j]->sample_id = -1;
        slot[j]->sample_type = -1;
    }

    info->sample_banks[bank_num]->slot = slot;
    info->sample_banks[bank_num]->gui_slot = gui_slot;

    GtkWidget *sb = info->sample_bank_pad;
    GtkWidget *frame = gtk_frame_new(frame_label);
    GtkWidget *label = gtk_label_new( str_label );

    gtk_container_set_border_width( GTK_CONTAINER( frame), 1 );
    gtk_widget_show(frame);
    info->sample_banks[bank_num]->page_num = gtk_notebook_append_page(GTK_NOTEBOOK(info->sample_bank_pad), frame, label);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_homogeneous(GTK_GRID(grid),TRUE);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid),TRUE);

    gtk_container_add( GTK_CONTAINER(frame), grid );

    gint col, row;
    for( row = 0; row < SAMPLEBANK_ROWS; row ++ ) {
        for( col = 0; col < SAMPLEBANK_COLUMNS; col ++ ) {
            int slot_nr = row * SAMPLEBANK_COLUMNS + col;
            create_slot( bank_num, slot_nr ,info->image_dimensions[0], info->image_dimensions[1]);
            sample_gui_slot_t *gui_slot = info->sample_banks[bank_num]->gui_slot[slot_nr];
            gtk_grid_attach( GTK_GRID(grid), gui_slot->event_box, col, row, 1,1);
            set_tooltip_by_widget( gui_slot->frame, tooltips[TOOLTIP_SAMPLESLOT].text);
        }
    }
    g_signal_connect(grid,"size-allocate",G_CALLBACK(samplebank_size_allocate), NULL);

    gtk_widget_show(grid);
    gtk_widget_show(sb );

    if( !info->normal )
    {
        info->normal = widget_get_fg( GTK_WIDGET(info->sample_banks[bank_num]->gui_slot[0]->frame) );
    }
    return bank_num;
}

void reset_samplebank(void)
{
    info->selection_slot = NULL;
    info->selection_gui_slot = NULL;
    info->selected_slot = NULL;
    info->selected_gui_slot = NULL;
    int i,j;
    for( i = 0; i < NUM_BANKS; i ++ )
    {
        if(info->sample_banks[i])
        {
            for(j = 0; j < NUM_SAMPLES_PER_PAGE ; j ++ )
            {
                update_sample_slot_data( i,j,0,0,NULL,NULL );
            }
        }
    }
}

void clear_samplebank_pages()
{
    while( gtk_notebook_get_n_pages(GTK_NOTEBOOK(info->sample_bank_pad) ) > 0 )
        gtk_notebook_remove_page( GTK_NOTEBOOK(info->sample_bank_pad), -1 );
}

void free_samplebank(void)
{
    int i,j;

    info->selection_slot = NULL;
    info->selection_gui_slot = NULL;
    info->selected_slot = NULL;
    info->selected_gui_slot = NULL;

    for( i = 0; i < NUM_BANKS; i ++ )
    {
        if(info->sample_banks[i])
        {
            /* free memory in use */
            for(j = 0; j < NUM_SAMPLES_PER_PAGE ; j ++ )
            {
                sample_slot_t *slot = info->sample_banks[i]->slot[j];
                sample_gui_slot_t *gslot = info->sample_banks[i]->gui_slot[j];
                if(slot->title) {
                    free(slot->title);
                    slot->title = NULL;
                }
                if(slot->timecode) {
                    free(slot->timecode);
                    slot->timecode = NULL;
                }
                if(slot->pixbuf) {
                    g_object_unref(slot->pixbuf);
                    slot->pixbuf = NULL;
                }
                free(slot);
                free(gslot);

                info->sample_banks[i]->slot[j] = NULL;
                info->sample_banks[i]->gui_slot[j] = NULL;
            }
            free(info->sample_banks[i]);
            info->sample_banks[i] = NULL;
        }
   }
}

static int dont_grow = 0;
static int bank_img_w = 0;
static int bank_img_h = 0;

// approximate best image size for sample slot
void setup_samplebank(gint num_cols, gint num_rows, GtkAllocation *allocation, int *idx, int *idy)
{
    gint bank_wid = allocation->width;
    gint bank_hei = allocation->height;

    bank_hei -= ((12 + 12) * num_rows); // there are 2 labels on each slot
    if( bank_hei < 0 )
        bank_hei = 1;
    bank_wid -= (8 * num_cols); // there is some spacing between the columns

    gint image_height = bank_hei / num_rows;
    gint image_width = bank_wid / num_cols;
   
    float ratio = (float) info->el.height / (float) info->el.width;
    float w,h;
   
    if( ratio <= 1.0f ) {
        h = ratio * image_width;
        w = image_width;
    }
    else {
        h = image_height;
        w = image_width / ratio;
    }


    if( image_height < h ) {
        w = (w / h) * image_height;
        h = image_height;
    }

    if( image_width < w ) {
        h = (h / w) * image_width;
        w = image_width;
    }

    if(dont_grow == 0) {
        bank_img_w = (int) w;
        bank_img_h = (int) h;
        *idx = bank_img_w;
        *idy = bank_img_h;
        dont_grow = 1;
    }
    else {
        *idx = (int) bank_img_w;
        *idy = (int) bank_img_h;
    }
}

/* --------------------------------------------------------------------------------------------------------------------------
 *  Function that resets the visualized sample-informations of the samplebanks, it does this by going through all
 *  slots that allready used and resets them (which means cleaning the shown infos as well as set them free for further use)
 *  with_selection should be TRUE when the actual selection of a sample-bank-slot should also be reseted
 *  (what is for instance necessary when vj reconnected)
   -------------------------------------------------------------------------------------------------------------------------- */
static int bank_exists( int bank_page, int slot_num )
{
    if(!info->sample_banks[bank_page])
        return 0;
    return 1;
}

static sample_slot_t *find_slot_by_sample( int sample_id , int sample_type )
{
    char key[32];
    sprintf(key, "S%04d%02d",sample_id, sample_type );

    void *slot = NULL;
    vevo_property_get( bankport_, key, 0,&slot );
    if(!slot)
        return NULL;
    return (sample_slot_t*) slot;
}

static sample_gui_slot_t *find_gui_slot_by_sample( int sample_id , int sample_type )
{
    char key[32];
    sprintf(key, "G%04d%02d",sample_id, sample_type );

    void *slot = NULL;
    vevo_property_get( bankport_, key, 0,&slot );
    if(!slot)
        return NULL;
    return (sample_gui_slot_t*) slot;
}

static int find_bank_by_sample(int sample_id, int sample_type, int *slot )
{
    int i,j;

    for( i = 0; i < NUM_BANKS; i ++ )
    {
        if(!info->sample_banks[i])
        {
            continue;
        }

        for( j = 0; j < NUM_SAMPLES_PER_PAGE; j ++ )
        {
            if(info->sample_banks[i]->slot[j]->sample_id == sample_id &&
               info->sample_banks[i]->slot[j]->sample_type == sample_type)
            {
                *slot = j;
#ifdef STRICT_CHECKING
                veejay_msg(VEEJAY_MSG_DEBUG, "using existing slot (%d,%d)",
                        sample_id,sample_type );
#endif
                return i;
            }
        }
    }

    for( i = 0; i < NUM_BANKS; i ++ )
    {
        if(!info->sample_banks[i])
        {
            *slot = 0;
            return i;
        }

        for( j = 0; j < NUM_SAMPLES_PER_PAGE; j ++ )
        {
             if ( info->sample_banks[i]->slot[j]->sample_id <= 0)
            {
                *slot = j;
#ifdef STRICT_CHECKING
                veejay_msg(VEEJAY_MSG_DEBUG, "using new slot (%d,%d)",
                            sample_id,sample_type);
#endif
                return i;
            }
        }
    }

    *slot = -1;
    return -1;
}

static int find_bank(int page_nr)
{
    int i = 0;
    for ( i = 0 ; i < NUM_BANKS; i ++ )
        if( info->sample_banks[i] && info->sample_banks[i]->page_num == page_nr )
        {
            return info->sample_banks[i]->bank_number;
        }
    return -1;
}

static void set_activation_of_cache_slot_in_samplebank(sequence_gui_slot_t *gui_slot,
                                                       gboolean activate)
{
    if (activate)
    {
        gtk_frame_set_shadow_type(GTK_FRAME(gui_slot->frame),GTK_SHADOW_IN);
    }
    else {
        gtk_frame_set_shadow_type(GTK_FRAME(gui_slot->frame),GTK_SHADOW_ETCHED_IN);
    }
}

static gboolean on_sequencerslot_activated_by_mouse(GtkWidget *widget,
                                                    GdkEventButton *event,
                                                    gpointer user_data)
{
    gint slot_nr = GPOINTER_TO_INT(user_data);

    if( event->type == GDK_BUTTON_PRESS && (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK )
    {
        multi_vims( VIMS_SEQUENCE_DEL, "%d", slot_nr );
        gtk_label_set_text(GTK_LABEL(info->sequencer_view->gui_slot[slot_nr]->image),
                           NULL );
    }
    else
    if(event->type == GDK_BUTTON_PRESS)
    {
        int id = info->status_tokens[CURRENT_ID];
        int type=info->status_tokens[STREAM_TYPE];
        if( info->selection_slot ) {
            id = info->selection_slot->sample_id;
            type=info->selection_slot->sample_type;
        }
        multi_vims( VIMS_SEQUENCE_ADD, "%d %d %d", slot_nr, id,type );
        info->uc.reload_hint[HINT_SEQ_ACT] = 1;
    }
    return FALSE;
}

static gboolean on_cacheslot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    gint slot_nr = -1;
    if(info->status_tokens[PLAY_MODE] == MODE_PLAIN )
        return FALSE;

    slot_nr =GPOINTER_TO_INT( user_data );
    set_activation_of_cache_slot_in_samplebank( info->sequence_view->gui_slot[slot_nr], FALSE );

    if( event->type == GDK_BUTTON_PRESS && (event->state & GDK_SHIFT_MASK) == GDK_SHIFT_MASK )
    {
        info->current_sequence_slot = slot_nr;
        sample_slot_t *s = info->selected_slot;
        sequence_gui_slot_t *g = info->sequence_view->gui_slot[slot_nr];
#ifdef STRICT_CHECKING
        assert( s != NULL );
        assert( g != NULL );
#endif
        g->sample_id = s->sample_id;
        g->sample_type = s->sample_type;
        vj_msg(VEEJAY_MSG_INFO, "Placed %s %d in Memory slot %d",
            (g->sample_type == 0 ? "Sample" : "Stream" ), g->sample_id, slot_nr );
    }
    else
    if(event->type == GDK_BUTTON_PRESS)
    {
        sequence_gui_slot_t *g = info->sequence_view->gui_slot[slot_nr];
        if(g->sample_id <= 0)
        {
            vj_msg(VEEJAY_MSG_ERROR, "Memory slot %d empty, put with SHIFT + mouse button1",slot_nr);
            return FALSE;
        }
        else 
        {
            vj_msg(VEEJAY_MSG_INFO,
               "Start playing %s %d",
               (g->sample_type==0 ? "Sample" : "Stream" ), g->sample_id );
        }
        multi_vims(VIMS_SET_MODE_AND_GO, "%d %d", g->sample_type, g->sample_id );
        vj_midi_learning_vims_msg2( info->midi, NULL, VIMS_SET_MODE_AND_GO, g->sample_type,g->sample_id );
    }
    return FALSE;
}

static void create_sequencer_slots(int nx, int ny)
{
    GtkWidget *vbox = glade_xml_get_widget_ (info->main_window, "SampleSequencerBox");
    info->sample_sequencer = gtk_frame_new(NULL);
    add_class(info->sample_sequencer, "sequencer");
    gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET(info->sample_sequencer), TRUE, TRUE, 0);
    gtk_widget_show(info->sample_sequencer);

    info->sequencer_view = (sequence_envelope*) vj_calloc(sizeof(sequence_envelope) );
    info->sequencer_view->gui_slot = (sequence_gui_slot_t**) vj_calloc(sizeof(sequence_gui_slot_t*) * ( nx * ny + 1 ) );

    GtkWidget *table = gtk_table_new( nx, ny, TRUE );

    gtk_container_add( GTK_CONTAINER(info->sample_sequencer), table );
    gtk_widget_show(table);

    info->sequencer_col = nx;
    info->sequencer_row = ny;

    gint col=0;
    gint row=0;
    gint k = 0;
    for( col = 0; col < ny; col ++ )
    for( row = 0; row < nx; row ++ )
    {
        sequence_gui_slot_t *gui_slot = (sequence_gui_slot_t*)vj_calloc(sizeof(sequence_gui_slot_t));
        info->sequencer_view->gui_slot[k] = gui_slot;

        gui_slot->event_box = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(gui_slot->event_box), TRUE);
        gtk_widget_set_can_focus(gui_slot->event_box, TRUE);

        g_signal_connect( G_OBJECT(gui_slot->event_box),
            "button_press_event",
            G_CALLBACK(on_sequencerslot_activated_by_mouse), //@@@@
            (gpointer) castIntToGpointer(k)
            );
        gtk_widget_show(GTK_WIDGET(gui_slot->event_box));

        gui_slot->frame = gtk_frame_new(NULL);
        gtk_container_set_border_width (GTK_CONTAINER(gui_slot->frame),0);
        gtk_frame_set_shadow_type(GTK_FRAME( gui_slot->frame), GTK_SHADOW_IN );
        gtk_widget_show(GTK_WIDGET(gui_slot->frame));
        gtk_container_add (GTK_CONTAINER (gui_slot->event_box), gui_slot->frame);
        add_class(gui_slot->frame, "sequencer_slot");

        /* the slot main container */
        gui_slot->main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
        gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->main_vbox);
        gtk_widget_show( GTK_WIDGET(gui_slot->main_vbox) );

        gui_slot->image = gtk_label_new(NULL);
        gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
        gtk_widget_show( gui_slot->image);
        gtk_table_attach_defaults ( GTK_TABLE(table), gui_slot->event_box, row, row+1, col, col+1);
        k++;
    }
//  gtk_widget_set_size_request_( table, 300,300);
//  info->sequencer_view->envelope_size = envelope_size;
}

static void create_ref_slots(int envelope_size)
{
    GtkWidget *vbox = glade_xml_get_widget_ (info->main_window, "quickselect");
    info->quick_select = gtk_frame_new(NULL);
    gtk_box_pack_start( GTK_BOX( vbox ), GTK_WIDGET(info->quick_select), TRUE, TRUE, 0);
    gtk_widget_show(info->quick_select);
    info->sequence_view = (sequence_envelope*) vj_calloc(sizeof(sequence_envelope) );
    info->sequence_view->gui_slot = (sequence_gui_slot_t**) vj_calloc(sizeof(sequence_gui_slot_t*) * envelope_size );
    GtkWidget *table = gtk_table_new( 1, envelope_size, TRUE );
    gtk_container_add( GTK_CONTAINER(info->quick_select), table );
    gtk_widget_show(table);

    gint col=0;
    gint row=0;
    for( row = 0; row < envelope_size; row ++ )
    {
        sequence_gui_slot_t *gui_slot = (sequence_gui_slot_t*)vj_calloc(sizeof(sequence_gui_slot_t));
        info->sequence_view->gui_slot[row] = gui_slot;
        gui_slot->event_box = gtk_event_box_new();
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(gui_slot->event_box), TRUE);
        gtk_widget_set_can_focus(gui_slot->event_box, TRUE);
        /* Right mouse button is popup menu, click = play */
        g_signal_connect( G_OBJECT(gui_slot->event_box),
            "button_press_event",
            G_CALLBACK(on_cacheslot_activated_by_mouse),
            (gpointer) castIntToGpointer(row)
            );
        gtk_widget_show(GTK_WIDGET(gui_slot->event_box));
        /* the surrounding frame for each slot */
        gui_slot->frame = gtk_frame_new(NULL);
        add_class(gui_slot->frame, "quickselect_slot");
        set_tooltip_by_widget(gui_slot->frame, tooltips[TOOLTIP_QUICKSELECT].text );
        gtk_widget_show(GTK_WIDGET(gui_slot->frame));
        gtk_container_add (GTK_CONTAINER (gui_slot->event_box), gui_slot->frame);

        /* the slot main container */
        gui_slot->main_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
        gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->main_vbox);
        gtk_widget_show( GTK_WIDGET(gui_slot->main_vbox) );

        /* The sample's image */
        gui_slot->image = gtk_image_new();
        gtk_box_pack_start (GTK_BOX (gui_slot->main_vbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
        gtk_widget_show( GTK_WIDGET(gui_slot->image));

        gtk_table_attach_defaults ( GTK_TABLE(table), gui_slot->event_box, row, row+1, col, col+1);
    }
    info->sequence_view->envelope_size = envelope_size;
}

static void create_slot(gint bank_nr, gint slot_nr, gint w, gint h)
{
    sample_bank_t **sample_banks = info->sample_banks;
    sample_gui_slot_t *gui_slot = sample_banks[bank_nr]->gui_slot[slot_nr];

    // to reach clicks on the following GUI-Elements of one slot, they are packed into an event_box
    gui_slot->event_box = gtk_event_box_new();
    gtk_event_box_set_visible_window(GTK_EVENT_BOX(gui_slot->event_box), TRUE);

    gtk_widget_set_can_focus(gui_slot->event_box, TRUE);
    g_signal_connect( G_OBJECT(gui_slot->event_box),
        "button_press_event",
        G_CALLBACK(on_slot_activated_by_mouse),
        (gpointer) castIntToGpointer(slot_nr)
        );
    gtk_widget_show(GTK_WIDGET(gui_slot->event_box));
    /* the surrounding frame for each slot */
    gui_slot->frame = gtk_frame_new(NULL);
    add_class(gui_slot->frame, "sample_slot");
    gtk_container_set_border_width (GTK_CONTAINER(gui_slot->frame),0);
    gtk_widget_show(GTK_WIDGET(gui_slot->frame));
    gtk_container_add (GTK_CONTAINER (gui_slot->event_box), GTK_WIDGET(gui_slot->frame));

    /* the slot main container */
    gui_slot->upper_hbox = gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    gtk_container_add (GTK_CONTAINER (gui_slot->frame), gui_slot->upper_hbox);
 
    gui_slot->title = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(gui_slot->title), 0.5, 0.0);
    gtk_box_pack_start( GTK_BOX(gui_slot->upper_hbox), GTK_WIDGET(gui_slot->title), FALSE, FALSE, 0 );

    gui_slot->image = gtk_image_new();
    gtk_box_pack_start (GTK_BOX (gui_slot->upper_hbox), GTK_WIDGET(gui_slot->image), TRUE, TRUE, 0);
   
    gui_slot->hotkey = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(gui_slot->hotkey), 0.5, 0.0);
    gtk_misc_set_padding (GTK_MISC(gui_slot->hotkey), 0,0 );
    gtk_box_pack_start (GTK_BOX (gui_slot->upper_hbox), GTK_WIDGET(gui_slot->hotkey), FALSE, FALSE, 0);

    gui_slot->timecode = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(gui_slot->timecode), 0.5, 0.0);
    gtk_misc_set_padding (GTK_MISC(gui_slot->timecode), 0,0 );
    gtk_box_pack_start (GTK_BOX (gui_slot->upper_hbox), GTK_WIDGET(gui_slot->timecode), FALSE, FALSE, 0);

    gtk_widget_show_all(GTK_WIDGET(gui_slot->upper_hbox));

}


/* -----------------------------------------------------------------------------
 *  Handler of mouse clicks on the GUI-elements of one slot
 *
 * - FIRST BUTTON
 *  single-click : activates the slot and the loaded sample (if there is one)
 *  single-click + modifier shift : select it has current channel source
 *  double-click or tripple-click : activates it and plays it immediatelly
 *
 * --------------------------------------------------------------------------- */
static gboolean on_slot_activated_by_mouse (GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
    gint bank_nr = -1;
    gint slot_nr = -1;

    bank_nr = find_bank( gtk_notebook_get_current_page(GTK_NOTEBOOK(info->sample_bank_pad)));
    if(bank_nr < 0 )
        return FALSE;

    slot_nr = GPOINTER_TO_INT(user_data);
    sample_bank_t **sample_banks = info->sample_banks;

    if( info->sample_banks[ bank_nr ]->slot[ slot_nr ]->sample_id <= 0 )
        return FALSE;

    sample_slot_t *select_slot = sample_banks[bank_nr]->slot[slot_nr];
    if( event->type == GDK_2BUTTON_PRESS )
    {

        multi_vims( VIMS_SET_MODE_AND_GO,
                    "%d %d",
                    (select_slot->sample_type==MODE_SAMPLE? MODE_SAMPLE:MODE_STREAM),
                    select_slot->sample_id);
        vj_midi_learning_vims_msg2( info->midi,
                                    NULL,
                                    VIMS_SET_MODE_AND_GO,
                                    select_slot->sample_type,
                                    select_slot->sample_id );
        vj_msg(VEEJAY_MSG_INFO,
               "Start playing %s %d",
               (select_slot->sample_type==MODE_SAMPLE ? "Sample" : "Stream" ),
               select_slot->sample_id );
    }
    else if(event->type == GDK_BUTTON_PRESS )
    {
        switch(event->state & GDK_SHIFT_MASK){
            case GDK_SHIFT_MASK :
            {
                multi_vims( VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
                    "%d %d %d %d",
                    0,
                    info->uc.selected_chain_entry,
                    select_slot->sample_type,
                    select_slot->sample_id );

                vj_msg(VEEJAY_MSG_INFO,
                       "Set mixing channel %d to %s %d",
                       info->uc.selected_chain_entry,
                       (select_slot->sample_type==MODE_SAMPLE ? "Sample" : "Stream" ),
                       select_slot->sample_id );

                char trip[100];
                snprintf(trip, sizeof(trip), "%03d:%d %d %d %d",VIMS_CHAIN_ENTRY_SET_SOURCE_CHANNEL,
                    0,
                    info->uc.selected_chain_entry,
                    select_slot->sample_type,
                    select_slot->sample_id );

                vj_midi_learning_vims( info->midi, NULL, trip, 0 );
            }
            break;

            default :
                set_selection_of_slot_in_samplebank(FALSE);
                info->selection_slot = select_slot;
                info->selection_gui_slot = sample_banks[bank_nr]->gui_slot[slot_nr];
                set_selection_of_slot_in_samplebank(TRUE );
            break;
        }
    }
    return FALSE;
}

static void indicate_sequence( gboolean active, sequence_gui_slot_t *slot )
{
    if(!active) {
        remove_class( slot->frame, "active");
    }
    else {
        add_class( slot->frame, "active");
    }
}

static void set_activation_of_slot_in_samplebank( gboolean activate)
{
    if(!info->selected_gui_slot || !info->selected_slot )
        return;

    if (activate)
    {
        if( (info->status_tokens[CURRENT_ID] == info->selected_slot->sample_id &&
        info->status_tokens[PLAY_MODE] == MODE_SAMPLE &&
        info->selected_slot->sample_type == 0 ) ||
        (info->status_tokens[CURRENT_ID] == info->selected_slot->sample_id &&
        info->status_tokens[PLAY_MODE] == MODE_STREAM &&
        info->selected_slot->sample_type != 0 ) 
       ) {
            gtk_widget_grab_focus(GTK_WIDGET(info->selected_gui_slot->frame));
            if( info->selection_gui_slot ) {
                // double click event also submitted button press event
                remove_class(info->selection_gui_slot->frame, "selected");
            }
            add_class(info->selected_gui_slot->frame, "active");
        }
    }
    else {
        remove_class(info->selected_gui_slot->frame, "active");
    }
}

static void set_selection_of_slot_in_samplebank(gboolean active)
{
    if(!info->selection_gui_slot || !info->selection_slot )
        return;

    if( (info->status_tokens[CURRENT_ID] == info->selection_slot->sample_id &&
        info->status_tokens[PLAY_MODE] == MODE_SAMPLE &&
        info->selection_slot->sample_type == 0 ) ||
        (info->status_tokens[CURRENT_ID] == info->selection_slot->sample_id &&
        info->status_tokens[PLAY_MODE] == MODE_STREAM &&
        info->selection_slot->sample_type != 0 ) 
       ) {
        return;
    }

    if(active) {
        add_class(info->selection_gui_slot->frame, "selected");
    }
    else {
        remove_class(info->selection_gui_slot->frame, "selected");
    }
}

static int add_sample_to_sample_banks(int bank_page,sample_slot_t *slot)
{
    int bp = 0; int s = 0;
#ifdef STRICT_CHECKING
    int result = verify_bank_capacity( &bp, &s, slot->sample_id, slot->sample_type );

    if( result )
        update_sample_slot_data( bp, s, slot->sample_id,slot->sample_type,slot->title,slot->timecode);

#else
    if(verify_bank_capacity( &bp, &s, slot->sample_id, slot->sample_type ))
        update_sample_slot_data( bp, s, slot->sample_id,slot->sample_type,slot->title,slot->timecode);
#endif
    return 1;
}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Removes a selected sample from the specific sample-bank-slot and update the free_slots-GList as well as
   -------------------------------------------------------------------------------------------------------------------------- */
static void remove_sample_from_slot()
{
    gint bank_nr = -1;
    gint slot_nr = -1;

    bank_nr = find_bank( gtk_notebook_get_current_page(
        GTK_NOTEBOOK( info->sample_bank_pad ) ) );
    if(bank_nr < 0 )
        return;
    if(!info->selection_slot)
        return;

    slot_nr = info->selection_slot->slot_number;

    if( info->selection_slot->sample_id == info->status_tokens[CURRENT_ID] &&
        info->selection_slot->sample_type == info->status_tokens[STREAM_TYPE] )
    {
        gchar error_msg[100];
        sprintf(error_msg, "Cannot delete %s %d while playing",
            (info->selection_slot->sample_type == MODE_SAMPLE ? "Sample" : "Stream" ),
            info->selection_slot->sample_id );
        message_dialog( "Error while deleting", error_msg );

        return;
    }

    multi_vims( (info->selection_slot->sample_type == 0 ? VIMS_SAMPLE_DEL :
                     VIMS_STREAM_DELETE ),
               "%d",
               info->selection_slot->sample_id );

    update_sample_slot_data( bank_nr, slot_nr, 0, -1, NULL, NULL);

    sample_gui_slot_t *gui_slot = info->sample_banks[bank_nr]->gui_slot[slot_nr];
    if(gui_slot)
        gtk_image_clear( GTK_IMAGE( gui_slot->image) );

    info->uc.reload_hint[HINT_SLIST] = 2;
}


/* --------------------------------------------------------------------------------------------------------------------------
 *  Function adds the given infos to the list of effect-sources
   -------------------------------------------------------------------------------------------------------------------------- */
static void add_sample_to_effect_sources_list(gint id,
                                              gint type,
                                              gchar *title,
                                              gchar *timecode)
{
    gchar id_string[512];
    GtkTreeIter iter;

    if (type == STREAM_NO_STREAM)
        snprintf( id_string,sizeof(id_string), "S[%4d] %s", id, title);
    else
        snprintf( id_string,sizeof(id_string), "T[%4d]", id);

    gtk_list_store_append( effect_sources_store, &iter );
    gtk_list_store_set(effect_sources_store, &iter,
                       SL_ID, id_string,
                       SL_DESCR, title,
                       SL_TIMECODE , timecode,
                       -1 );

    GtkTreeIter iter2;
    if(type == STREAM_NO_STREAM)
    {
        gtk_list_store_append( cali_sourcestore,&iter2);
        gtk_list_store_set(cali_sourcestore,&iter2,
                           SL_ID, id_string,
                           SL_DESCR,title,
                           SL_TIMECODE,timecode,
                           -1);
    }
}

/*
    Update a slot, either set from function arguments or clear it
 */
static void update_sample_slot_data(int page_num,
                                    int slot_num,
                                    int sample_id,
                                    gint sample_type,
                                    gchar *title,
                                    gchar *timecode)
{
    sample_slot_t *slot = info->sample_banks[page_num]->slot[slot_num];
    sample_gui_slot_t *gui_slot = info->sample_banks[page_num]->gui_slot[slot_num];

    if(slot->timecode) { free(slot->timecode); slot->timecode = NULL; }
    if(slot->title) { free(slot->title); slot->title = NULL; }

    slot->sample_id = sample_id;
    slot->sample_type = sample_type;

    slot->timecode = timecode == NULL ? NULL : strduplastn( timecode );
    slot->title = title == NULL ? NULL : strduplastn( title );

    if( sample_id > 0)
    {
        char sample_key[32];
        snprintf(sample_key,sizeof(sample_key), "S%04d%02d", sample_id, sample_type );
        vevo_property_set( bankport_, sample_key, VEVO_ATOM_TYPE_VOIDPTR,1, &slot );
        snprintf(sample_key,sizeof(sample_key), "G%04d%02d", sample_id, sample_type );
        vevo_property_set( bankport_, sample_key, VEVO_ATOM_TYPE_VOIDPTR,1,&gui_slot);
    }

    if(gui_slot)
    {
        if(sample_id > 0 )
        {
            char hotkey[16];
            if( sample_type == MODE_SAMPLE ) {
                snprintf(hotkey, sizeof(hotkey), "[F%d] Sample %d", (sample_id % 12), sample_id);
            }
            else{
                snprintf(hotkey, sizeof(hotkey), "[F%d] Stream %d", (sample_id % 12), sample_id);
            }
            gtk_label_set_text( GTK_LABEL( gui_slot->title ), slot->title );
            gtk_label_set_text( GTK_LABEL( gui_slot->hotkey ), hotkey );
            gtk_label_set_text( GTK_LABEL( gui_slot->timecode ), slot->timecode );
        }
        else
        {
            gtk_label_set_text( GTK_LABEL(gui_slot->title), "" );
            gtk_label_set_text( GTK_LABEL(gui_slot->hotkey), "" );
            gtk_label_set_text( GTK_LABEL(gui_slot->timecode), "" );
        }
    }

    if( sample_id == 0 )
    {
        if(slot->pixbuf)
        {
            g_object_unref( slot->pixbuf );
            slot->pixbuf = NULL;
        }
    }
}

void veejay_release_track(int id, int release_this)
{
    multitrack_release_track( info->mt, id, release_this );
}

void veejay_bind_track( int id, int bind_this )
{
    multitrack_bind_track(info->mt, id, bind_this );
    info->uc.reload_hint[HINT_SLIST] = 2;
}
