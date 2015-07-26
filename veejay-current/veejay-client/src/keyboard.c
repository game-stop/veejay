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
#include <config.h>
#include <gtk/gtk.h>
#ifdef HAVE_SDL
#include "keyboard.h"
static struct
{
	const int	sdl_mod;
	const int	gdk_mod;
	const gchar	*title;
} modifier_translation_table_t[] = 
{
	{	0,	0,			" "  },
	{	0,	16,			" "  },
	{	3,	1,			"shift" },
	{	3,	17,			"shift"	},
	{	1, 	8,			"alt"	},
	{	1,	24,			"alt"	},
	{	1,	44,			"alt"	},
	{	2, 	4,			"ctrl"	},
	{	2,	20,			"ctrl"	},
	{	0,	0,			NULL	},
};
/* fixme: introduce keyboard mapping functionality
        1. attach VIMS events/VIMS BUNDLES to keys (with arguments)
	2. save/load VIMS keyboard mapping
*/
static struct
{
	const int gdk_sym;			// GDK key
	const int sdl_sym;			// SDL key
	const gchar   *title;				// plain text
} key_translation_table_t[] =
{

{	GDK_space,	SDLK_SPACE,		"Space"		},
{	GDK_exclam,	SDLK_EXCLAIM,		"Exclaim"	},
{	GDK_quotedbl,	SDLK_QUOTEDBL,		"Double quote"  },
{	GDK_numbersign, SDLK_HASH,		"Hash"		},
{	GDK_dollar,	SDLK_DOLLAR,		"Dollar"	},
{	GDK_percent,	SDLK_PAUSE,		"Percent"	},
{	GDK_parenleft,	SDLK_LEFTPAREN,		"Leftparen"	},
{	GDK_parenright, SDLK_RIGHTPAREN,	"Rightparen"	},
{	GDK_asciicircum,SDLK_CARET,		"Caret"		},
{	GDK_ampersand,  SDLK_AMPERSAND,		"Ampersand"	},
{	GDK_underscore,	SDLK_UNDERSCORE,	"Underscore"	},
{	GDK_braceright, 123,			"Rightbrace"	},
{	GDK_braceleft,  125,			"Leftbrace"	},
{	GDK_grave, 	SDLK_BACKQUOTE,		"Aphostrophe"	},
{	GDK_asciitilde, 126,			"Tilde"		},
{	GDK_asterisk,	SDLK_ASTERISK,		"Asterisk"	},
{	GDK_plus,	SDLK_PLUS,		"Plus"		},
{	GDK_comma,	SDLK_COMMA,		"Comma"		},
{	GDK_minus,	SDLK_MINUS,		"Minus"		},
{	GDK_period,	SDLK_PERIOD,		"Period"	},
{	GDK_slash,	SDLK_SLASH,		"Slash"		},
{	GDK_Home,	SDLK_HOME,		"Home"		},
{	GDK_End,	SDLK_END,		"End"		},
{	GDK_Page_Up,	SDLK_PAGEUP,		"PageUp"	},
{	GDK_Page_Down,	SDLK_PAGEDOWN,		"PageDown"	},
{	GDK_Insert,	SDLK_INSERT,		"Insert"	},
{	GDK_Up,		SDLK_UP,		"Up"		},
{	GDK_Down,	SDLK_DOWN,		"Down"		},
{	GDK_Left,	SDLK_LEFT,		"Left"		},
{	GDK_Right,	SDLK_RIGHT,		"Right"		},
{	GDK_Tab,	SDLK_TAB,		"TAB"		},
{	GDK_BackSpace,	SDLK_BACKSPACE,		"Backspace"	},
{	GDK_Escape,	SDLK_ESCAPE,		"Escape"	},
{	GDK_Delete,	SDLK_DELETE,		"Delete"	},
{	GDK_F1,		SDLK_F1,		"F1"		},
{	GDK_F2,		SDLK_F2,		"F2"		},
{	GDK_F3,		SDLK_F3,		"F3"		},
{	GDK_F4,		SDLK_F4,		"F4"		},
{	GDK_F5,		SDLK_F5,		"F5"		},
{	GDK_F6,		SDLK_F6,		"F6"		},
{	GDK_F7,		SDLK_F7,		"F7"		},
{	GDK_F8,		SDLK_F8,		"F8"		},
{	GDK_F9,		SDLK_F9,		"F9"		},
{	GDK_F10,	SDLK_F10,		"F10"		},
{	GDK_F11,	SDLK_F11,		"F11"		},
{	GDK_F12,	SDLK_F12,		"F12"		},
{	GDK_EuroSign,	SDLK_EURO,		"Euro"		},
{	GDK_KP_0,	SDLK_KP0,		"keypad 0"	},
{	GDK_KP_1,	SDLK_KP1,		"keypad 1"	},
{	GDK_KP_2,	SDLK_KP2,		"keypad 2"	},
{	GDK_KP_3,	SDLK_KP3,		"keypad 3"	},
{	GDK_KP_4,	SDLK_KP4,		"keypad 4"	},
{	GDK_KP_5,	SDLK_KP5,		"keypad 5"	},
{	GDK_KP_6,	SDLK_KP6,		"keypad 6"	},
{	GDK_KP_7,	SDLK_KP7,		"keypad 7"	},
{	GDK_KP_8,	SDLK_KP8,		"keypad 8"	},
{	GDK_KP_9,	SDLK_KP9,		"keypad 9"	},
{	GDK_KP_Divide,	SDLK_KP_DIVIDE,		"keypad /"	},
{	GDK_KP_Multiply,SDLK_KP_MULTIPLY,	"keypad *"	},
{	GDK_KP_Subtract,SDLK_KP_MINUS,		"keypad -"	},
{	GDK_KP_Add,	SDLK_KP_PLUS,		"keypad +"	},
{	GDK_KP_Equal,	SDLK_KP_EQUALS,		"keypad ="	},
{	GDK_KP_Enter,	SDLK_KP_ENTER,		"keypad ENTER"	},
{	GDK_ISO_Enter,	SDLK_RETURN,		"ENTER"		},
{	GDK_3270_Enter, SDLK_RETURN,		"ENTER"		},  

/* GDK_KP doesnt word on all systems ... */
{	0xff9f,		SDLK_KP0,		"keypad 0"	},
{	0xff9c,		SDLK_KP1,		"keypad 1"	},
{	0xff99,		SDLK_KP2,		"keypad 2"	},
{	0xff9b,		SDLK_KP3,		"keypad 3"	},
{	0xff96,		SDLK_KP4,		"keypad 4"	},
{	0xff9d,		SDLK_KP5,		"keypad 5"	},
{	0xff98,		SDLK_KP6,		"keypad 6"	},
{	0xff95,		SDLK_KP7,		"keypad 7"	},
{	0xff97,		SDLK_KP8,		"keypad 8"	},
{	0xff9a,		SDLK_KP9,		"keypad 9"	},
{	0xff9f,		SDLK_KP_PERIOD,		"keypad ."	},


{	GDK_0,		SDLK_0,			"0"		},
{	GDK_1,		SDLK_1,			"1"		},
{	GDK_2,		SDLK_2,			"2"		},
{	GDK_3,		SDLK_3,			"3"		},
{	GDK_4,		SDLK_4,			"4"		},
{	GDK_5,		SDLK_5,			"5"		},
{	GDK_6,		SDLK_6,			"6"		},
{	GDK_7,		SDLK_7,			"7"		},
{	GDK_8,		SDLK_8,			"8"		},
{	GDK_9,		SDLK_9,			"9"		},
{	GDK_colon,	SDLK_COLON,		"colon"		},
{	GDK_semicolon,  SDLK_SEMICOLON,		"semicolon"	},
{	GDK_less,	SDLK_LESS,		"less"		},
{	GDK_equal,	SDLK_EQUALS,		"equals"	},
{	GDK_greater,	SDLK_GREATER,		"greater"	},
{	GDK_question,	SDLK_QUESTION,		"question"	},
{	GDK_at,		SDLK_AT,		"at"		},
{	GDK_bracketleft,SDLK_LEFTBRACKET,	"left bracket"	},
{	GDK_backslash, 	SDLK_BACKSLASH,		"backslash"	},
{	GDK_bracketright,SDLK_RIGHTBRACKET,	"right bracket" },
{	GDK_underscore, SDLK_UNDERSCORE,	"underscore" 	},
{	GDK_A,		SDLK_a,			"A"		},
{	GDK_B,		SDLK_b,			"B"		},
{	GDK_C,		SDLK_c,			"C"		},
{	GDK_D,		SDLK_d,			"D"		},
{	GDK_E,		SDLK_e,			"E"		},
{	GDK_F,		SDLK_f,			"F"		},
{	GDK_G,		SDLK_g,			"G"		},
{	GDK_H,		SDLK_h,			"H"		},
{	GDK_I,		SDLK_i,			"I"		},
{	GDK_J,		SDLK_j,			"J"		},
{	GDK_K,		SDLK_k,			"K"		},
{	GDK_L,		SDLK_l,			"L"		},
{	GDK_M,		SDLK_m,			"M"		},
{	GDK_N,		SDLK_n,			"N"		},
{	GDK_O,		SDLK_o,			"O"		},
{	GDK_P,		SDLK_p,			"P"		},
{	GDK_Q,		SDLK_q,			"Q"		},
{	GDK_R,		SDLK_r,			"R"		},
{	GDK_S,		SDLK_s,			"S"		},
{	GDK_T,		SDLK_t,			"T"		},
{	GDK_U,		SDLK_u,			"U"		},
{	GDK_V,		SDLK_v,			"V"		},
{	GDK_W,		SDLK_w,			"W"		},
{	GDK_X,		SDLK_y,			"Y"		},
{	GDK_Z,		SDLK_z,			"Z"		},
{	GDK_a,		SDLK_a,			"a"		},
{	GDK_b,		SDLK_b,			"b"		},
{	GDK_c,		SDLK_c,			"c"		},
{	GDK_d,		SDLK_d,			"d"		},
{	GDK_e,		SDLK_e,			"e"		},
{	GDK_f,		SDLK_f,			"f"		},
{	GDK_g,		SDLK_g,			"g"		},
{	GDK_h,		SDLK_h,			"h"		},
{	GDK_i,		SDLK_i,			"i"		},
{	GDK_j,		SDLK_j,			"j"		},
{	GDK_k,		SDLK_k,			"k"		},
{	GDK_l,		SDLK_l,			"l"		},
{	GDK_m,		SDLK_m,			"m"		},
{	GDK_n,		SDLK_n,			"n"		},
{	GDK_o,		SDLK_o,			"o"		},
{	GDK_p,		SDLK_p,			"p"		},
{	GDK_q,		SDLK_q,			"q"		},
{	GDK_r,		SDLK_r,			"r"		},
{	GDK_s,		SDLK_s,			"s"		},
{	GDK_t,		SDLK_t,			"t"		},
{	GDK_u,		SDLK_u,			"u"		},
{	GDK_v,		SDLK_v,			"v"		},
{	GDK_w,		SDLK_w,			"w"		},
{	GDK_x,		SDLK_x,			"x"		},
{	GDK_y,		SDLK_y,			"y"		},
{	GDK_z,		SDLK_z,			"z"		},
{	0,		0,			NULL		},

};


int		sdl2gdk_key(int sdl_key)
{
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

int		gdk2sdl_mod( int gdk_mod )
{
	int i;
	for ( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_mod == modifier_translation_table_t[i].gdk_mod )
			return modifier_translation_table_t[i].sdl_mod;
	}
	return 0;
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
			return modifier_translation_table_t[i].sdl_mod;
	}

	return 0;
}

int		sdlkey_by_name( gchar *name )
{
	int i;
	if(!name)
		return 0;
	for ( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( g_utf8_collate(name,
				key_translation_table_t[i].title) == 0)
			return key_translation_table_t[i].sdl_sym;
	}
	return 0;
}

gchar		*sdlkey_by_id( int sdl_key )
{
	int i;
	gchar *ret = NULL;
	for ( i = 0; key_translation_table_t[i].title != NULL ; i ++ )
	{
		if( sdl_key == key_translation_table_t[i].sdl_sym )
			return (gchar*)key_translation_table_t[i].title;
	}
	return ret;
}

gchar		*sdlmod_by_id( int sdl_mod )
{
	int i;
	gchar *ret = NULL;
	for ( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( sdl_mod == modifier_translation_table_t[i].sdl_mod )
			return (gchar*)modifier_translation_table_t[i].title;
	}
	return ret;
}
gchar		*gdkmod_by_id( int gdk_mod )
{
	int i;
	for( i = 0; modifier_translation_table_t[i].title != NULL ; i ++ )
	{
		if( gdk_mod == modifier_translation_table_t[i].gdk_mod ||
			gdk_mod & modifier_translation_table_t[i].gdk_mod )
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

/*
Key snooper functions are called before normal event delivery.
They can be used to implement custom key event handling.
grab_widget¿:	the widget to which the event will be delivered.
event¿:	the key event.
func_data¿:	the func_data supplied to gtk_key_snooper_install().
Returns¿:	TRUE to stop further processing of event, FALSE to continue.
*/

gboolean	key_snooper(GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
	return FALSE;
}
#endif
// gtk_key_snooper_install
