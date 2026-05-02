/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdio.h>
#include <config.h>
#include <gtk/gtk.h>
#ifdef HAVE_SDL
#include <gdk/gdkkeysyms.h>
#include <gdk/gdktypes.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_keycode.h>
#include "keyboard.h"


typedef enum {
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

static struct
{
	const int vims_mod;
	const int gdk_mod;
	const gchar *title;
} modifier_translation_table_t[] =
{
	{ VIMS_MOD_NONE, 0, "none" },
	{ VIMS_MOD_SHIFT, GDK_SHIFT_MASK, "shift" },
	{ VIMS_MOD_ALT, GDK_MOD1_MASK, "alt" },
	{ VIMS_MOD_CTRL, GDK_CONTROL_MASK, "ctrl" },
	{ VIMS_MOD_CAPSLOCK, GDK_LOCK_MASK, "capslock" },
	{ VIMS_MOD_CTRL | VIMS_MOD_ALT, GDK_CONTROL_MASK | GDK_MOD1_MASK, "ctrl+alt" },
	{ VIMS_MOD_CTRL | VIMS_MOD_SHIFT, GDK_CONTROL_MASK | GDK_SHIFT_MASK, "ctrl+shift" },
	{ VIMS_MOD_ALT  | VIMS_MOD_SHIFT, GDK_MOD1_MASK | GDK_SHIFT_MASK, "alt+shift" },
	{ VIMS_MOD_CTRL | VIMS_MOD_ALT | VIMS_MOD_SHIFT,
	  GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK, "ctrl+alt+shift" },
	{ 0, 0, NULL }
};

static struct
{
	const int gdk_sym;			// GDK key
	const int sdl_sym;			// SDL key
	const gchar   *title;				// plain text
} key_translation_table_t[] =
{

{	GDK_KEY_space,	SDLK_SPACE,		"Space"		},
{	GDK_KEY_exclam,	SDLK_EXCLAIM,		"Exclaim"	},
{	GDK_KEY_quotedbl,	SDLK_QUOTEDBL,		"Double quote"  },
{	GDK_KEY_numbersign, SDLK_HASH,		"Hash"		},
{	GDK_KEY_dollar,	SDLK_DOLLAR,		"Dollar"	},
{	GDK_KEY_percent,	SDLK_PERCENT,		"Percent"	},
{	GDK_KEY_parenleft,	SDLK_LEFTPAREN,		"Leftparen"	},
{	GDK_KEY_parenright, SDLK_RIGHTPAREN,	"Rightparen"	},
{	GDK_KEY_asciicircum,SDLK_CARET,		"Caret"		},
{	GDK_KEY_ampersand,  SDLK_AMPERSAND,		"Ampersand"	},
{	GDK_KEY_underscore,	SDLK_UNDERSCORE,	"Underscore"	},
{	GDK_KEY_braceright, 125,			"Rightbrace"	},
{	GDK_KEY_braceleft,  123,			"Leftbrace"	},
{	GDK_KEY_grave, 	SDLK_BACKQUOTE,		"Aphostrophe"	},
{	GDK_KEY_asciitilde, 126,			"Tilde"		},
{	GDK_KEY_asterisk,	SDLK_ASTERISK,		"Asterisk"	},
{	GDK_KEY_plus,	SDLK_PLUS,		"Plus"		},
{	GDK_KEY_comma,	SDLK_COMMA,		"Comma"		},
{	GDK_KEY_minus,	SDLK_MINUS,		"Minus"		},
{	GDK_KEY_period,	SDLK_PERIOD,		"Period"	},
{	GDK_KEY_slash,	SDLK_SLASH,		"Slash"		},
{	GDK_KEY_Home,	SDLK_HOME,		"Home"		},
{	GDK_KEY_End,	SDLK_END,		"End"		},
{	GDK_KEY_Page_Up,	SDLK_PAGEUP,		"PageUp"	},
{	GDK_KEY_Page_Down,	SDLK_PAGEDOWN,		"PageDown"	},
{	GDK_KEY_Insert,	SDLK_INSERT,		"Insert"	},
{	GDK_KEY_Up,		SDLK_UP,		"Up"		},
{	GDK_KEY_Down,	SDLK_DOWN,		"Down"		},
{	GDK_KEY_Left,	SDLK_LEFT,		"Left"		},
{	GDK_KEY_Right,	SDLK_RIGHT,		"Right"		},
{	GDK_KEY_Tab,	SDLK_TAB,		"TAB"		},
{	GDK_KEY_BackSpace,	SDLK_BACKSPACE,		"Backspace"	},
{	GDK_KEY_Escape,	SDLK_ESCAPE,		"Escape"	},
{	GDK_KEY_Delete,	SDLK_DELETE,		"Delete"	},
{	GDK_KEY_F1,		SDLK_F1,		"F1"		},
{	GDK_KEY_F2,		SDLK_F2,		"F2"		},
{	GDK_KEY_F3,		SDLK_F3,		"F3"		},
{	GDK_KEY_F4,		SDLK_F4,		"F4"		},
{	GDK_KEY_F5,		SDLK_F5,		"F5"		},
{	GDK_KEY_F6,		SDLK_F6,		"F6"		},
{	GDK_KEY_F7,		SDLK_F7,		"F7"		},
{	GDK_KEY_F8,		SDLK_F8,		"F8"		},
{	GDK_KEY_F9,		SDLK_F9,		"F9"		},
{	GDK_KEY_F10,	SDLK_F10,		"F10"		},
{	GDK_KEY_F11,	SDLK_F11,		"F11"		},
{	GDK_KEY_F12,	SDLK_F12,		"F12"		},
{	GDK_KEY_KP_0,	SDLK_KP_0,		"keypad 0"	},
{	GDK_KEY_KP_1,	SDLK_KP_1,		"keypad 1"	},
{	GDK_KEY_KP_2,	SDLK_KP_2,		"keypad 2"	},
{	GDK_KEY_KP_3,	SDLK_KP_3,		"keypad 3"	},
{	GDK_KEY_KP_4,	SDLK_KP_4,		"keypad 4"	},
{	GDK_KEY_KP_5,	SDLK_KP_5,		"keypad 5"	},
{	GDK_KEY_KP_6,	SDLK_KP_6,		"keypad 6"	},
{	GDK_KEY_KP_7,	SDLK_KP_7,		"keypad 7"	},
{	GDK_KEY_KP_8,	SDLK_KP_8,		"keypad 8"	},
{	GDK_KEY_KP_9,	SDLK_KP_9,		"keypad 9"	},
{	GDK_KEY_KP_Divide,	SDLK_KP_DIVIDE,		"keypad /"	},
{	GDK_KEY_KP_Multiply,SDLK_KP_MULTIPLY,	"keypad *"	},
{	GDK_KEY_KP_Subtract,SDLK_KP_MINUS,		"keypad -"	},
{	GDK_KEY_KP_Add,	SDLK_KP_PLUS,		"keypad +"	},
{	GDK_KEY_KP_Equal,	SDLK_KP_EQUALS,		"keypad ="	},
{	GDK_KEY_KP_Enter,	SDLK_KP_ENTER,		"keypad ENTER"	},
{	GDK_KEY_ISO_Enter,	SDLK_RETURN,		"ENTER"		},
{	GDK_KEY_3270_Enter, SDLK_RETURN,		"ENTER"		},  
{	0xff9f,		SDLK_KP_PERIOD,		"keypad ."	},
{	GDK_KEY_0,		SDLK_0,			"0"		},
{	GDK_KEY_1,		SDLK_1,			"1"		},
{	GDK_KEY_2,		SDLK_2,			"2"		},
{	GDK_KEY_3,		SDLK_3,			"3"		},
{	GDK_KEY_4,		SDLK_4,			"4"		},
{	GDK_KEY_5,		SDLK_5,			"5"		},
{	GDK_KEY_6,		SDLK_6,			"6"		},
{	GDK_KEY_7,		SDLK_7,			"7"		},
{	GDK_KEY_8,		SDLK_8,			"8"		},
{	GDK_KEY_9,		SDLK_9,			"9"		},
{	GDK_KEY_colon,	SDLK_COLON,		"colon"		},
{	GDK_KEY_semicolon,  SDLK_SEMICOLON,		"semicolon"	},
{	GDK_KEY_less,	SDLK_LESS,		"less"		},
{	GDK_KEY_equal,	SDLK_EQUALS,		"equals"	},
{	GDK_KEY_greater,	SDLK_GREATER,		"greater"	},
{	GDK_KEY_question,	SDLK_QUESTION,		"question"	},
{	GDK_KEY_at,		SDLK_AT,		"at"		},
{	GDK_KEY_bracketleft,SDLK_LEFTBRACKET,	"left bracket"	},
{	GDK_KEY_backslash, 	SDLK_BACKSLASH,		"backslash"	},
{	GDK_KEY_bracketright,SDLK_RIGHTBRACKET,	"right bracket" },
{	GDK_KEY_underscore, SDLK_UNDERSCORE,	"underscore" 	},
{	GDK_KEY_A,		SDLK_a,			"A"		},
{	GDK_KEY_B,		SDLK_b,			"B"		},
{	GDK_KEY_C,		SDLK_c,			"C"		},
{	GDK_KEY_D,		SDLK_d,			"D"		},
{	GDK_KEY_E,		SDLK_e,			"E"		},
{	GDK_KEY_F,		SDLK_f,			"F"		},
{	GDK_KEY_G,		SDLK_g,			"G"		},
{	GDK_KEY_H,		SDLK_h,			"H"		},
{	GDK_KEY_I,		SDLK_i,			"I"		},
{	GDK_KEY_J,		SDLK_j,			"J"		},
{	GDK_KEY_K,		SDLK_k,			"K"		},
{	GDK_KEY_L,		SDLK_l,			"L"		},
{	GDK_KEY_M,		SDLK_m,			"M"		},
{	GDK_KEY_N,		SDLK_n,			"N"		},
{	GDK_KEY_O,		SDLK_o,			"O"		},
{	GDK_KEY_P,		SDLK_p,			"P"		},
{	GDK_KEY_Q,		SDLK_q,			"Q"		},
{	GDK_KEY_R,		SDLK_r,			"R"		},
{	GDK_KEY_S,		SDLK_s,			"S"		},
{	GDK_KEY_T,		SDLK_t,			"T"		},
{	GDK_KEY_U,		SDLK_u,			"U"		},
{	GDK_KEY_V,		SDLK_v,			"V"		},
{	GDK_KEY_W,		SDLK_w,			"W"		},
{	GDK_KEY_X,		SDLK_y,			"X"		},
{	GDK_KEY_Y,		SDLK_y,			"Y"		},
{	GDK_KEY_Z,		SDLK_z,			"Z"		},
{	GDK_KEY_a,		SDLK_a,			"a"		},
{	GDK_KEY_b,		SDLK_b,			"b"		},
{	GDK_KEY_c,		SDLK_c,			"c"		},
{	GDK_KEY_d,		SDLK_d,			"d"		},
{	GDK_KEY_e,		SDLK_e,			"e"		},
{	GDK_KEY_f,		SDLK_f,			"f"		},
{	GDK_KEY_g,		SDLK_g,			"g"		},
{	GDK_KEY_h,		SDLK_h,			"h"		},
{	GDK_KEY_i,		SDLK_i,			"i"		},
{	GDK_KEY_j,		SDLK_j,			"j"		},
{	GDK_KEY_k,		SDLK_k,			"k"		},
{	GDK_KEY_l,		SDLK_l,			"l"		},
{	GDK_KEY_m,		SDLK_m,			"m"		},
{	GDK_KEY_n,		SDLK_n,			"n"		},
{	GDK_KEY_o,		SDLK_o,			"o"		},
{	GDK_KEY_p,		SDLK_p,			"p"		},
{	GDK_KEY_q,		SDLK_q,			"q"		},
{	GDK_KEY_r,		SDLK_r,			"r"		},
{	GDK_KEY_s,		SDLK_s,			"s"		},
{	GDK_KEY_t,		SDLK_t,			"t"		},
{	GDK_KEY_u,		SDLK_u,			"u"		},
{	GDK_KEY_v,		SDLK_v,			"v"		},
{	GDK_KEY_w,		SDLK_w,			"w"		},
{	GDK_KEY_x,		SDLK_x,			"x"		},
{	GDK_KEY_y,		SDLK_y,			"y"		},
{	GDK_KEY_z,		SDLK_z,			"z"		},
{	0,		0,			NULL		},

};


int sdl2gdk_key(int sdl_key)
{
    for (int i = 0; key_translation_table_t[i].title != NULL; i++)
    {
        if (sdl_key == key_translation_table_t[i].sdl_sym)
            return key_translation_table_t[i].gdk_sym;
    }
    return 0;
}

int		gdk2sdl_key(int gdk_key)
{
	int i;
	for ( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_key == key_translation_table_t[i].gdk_sym )
			return key_translation_table_t[i].sdl_sym;
	}
	return 0;
}

int gdk2sdl_mod(int gdk_mod)
{
    int result = VIMS_MOD_NONE;

    for(int i = 1; modifier_translation_table_t[i].title != NULL; i++)
    {
        if((gdk_mod & modifier_translation_table_t[i].gdk_mod) == modifier_translation_table_t[i].gdk_mod)
        {
            result |= modifier_translation_table_t[i].vims_mod;
        }
    }
    return result;
}

int		sdlmod_by_name( gchar *name )
{
	int i;
	if(!name)
		return 0;

	for ( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( g_utf8_collate(name,
				modifier_translation_table_t[i].title) == 0)
			return modifier_translation_table_t[i].vims_mod;
	}

	return 0;
}

int sdlkey_by_name(gchar *name)
{
	int i;

	if(!name)
		return 0;

	for(i = 0; key_translation_table_t[i].title != NULL; i++)
	{
		if(strcmp(name, key_translation_table_t[i].title) == 0)
			return key_translation_table_t[i].sdl_sym;
	}

	return 0;
}

gchar *sdlkey_by_id(int sdl_key) {
    for (int i = 0; key_translation_table_t[i].title != NULL; i++) {
        if (sdl_key == key_translation_table_t[i].sdl_sym) {
            return (gchar*)key_translation_table_t[i].title;
        }
    }
    return (gchar*)SDL_GetKeyName(sdl_key);
}

gchar *sdlmod_by_id(int vims_mod)
{
	for(int i = 0; modifier_translation_table_t[i].title != NULL; i++)
	{
		if(vims_mod == modifier_translation_table_t[i].vims_mod)
			return (gchar*)modifier_translation_table_t[i].title;
	}

	return NULL;
}

gchar *gdkmod_by_id(int gdk_mod)
{
	for(int i = 0; modifier_translation_table_t[i].title != NULL; i++)
	{
		if(gdk_mod == modifier_translation_table_t[i].gdk_mod)
			return (gchar*)modifier_translation_table_t[i].title;
	}

	return NULL;
}

gchar		*gdkkey_by_id( int gdk_key )
{
	int i;
	for( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_key == key_translation_table_t[i].gdk_sym )
			return (gchar*)key_translation_table_t[i].title;
	}
	return NULL;
}

gboolean	key_snooper(GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	return FALSE;
}
#endif
// gtk_key_snooper_install
