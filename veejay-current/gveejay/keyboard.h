#ifndef GDKSDL
#define GDKSDL

#include <gdk/gdkkeysyms.h>
#include <SDL/SDL_keysym.h>
#include <glib.h>
#include <stdint.h>
#include <stdio.h>

static struct
{
	const int	sdl_mod;
	const int	gdk_mod;
	const gchar	*title;
} modifier_translation_table_t[] = 
{
	{	0,	0,	"None"},
	{	3,	0,	"Shift" },
	{	1, 	0,	"CTRL"	},
	{	2, 	0,	"ALT"	},
	{	0,	0,	NULL		},
};


/*
	incomplete list of all keys
 */



static struct
{
	const uint8_t gdk_sym;			// GDK key
	const uint8_t sdl_sym;			// SDL key
	const gchar   *title;				// plain text
} key_translation_table_t[] =
{

{	GDK_space,	SDLK_SPACE,		"space"		},
{	GDK_exclam,	SDLK_EXCLAIM,		"exclaim"	},
{	GDK_quotedbl,	SDLK_QUOTEDBL,		"double quote"  },
{	GDK_numbersign, SDLK_DOLLAR,		"dollar"	},
{	GDK_percent,	SDLK_PAUSE,		"percent"	},
{	GDK_ampersand,  SDLK_AMPERSAND,		"ampersand"	},
{	GDK_apostrophe, SDLK_BACKQUOTE,		"aphostrophe"	},
{	GDK_plus,	SDLK_PLUS,		"plus"		},
{	GDK_comma,	SDLK_COMMA,		"comma"		},
{	GDK_minus,	SDLK_MINUS,		"minus"		},
{	GDK_period,	SDLK_PERIOD,		"period"	},
{	GDK_slash,	SDLK_SLASH,		"slash"		},
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
{	GDK_grave,	SDLK_CARET,		"caret"		},
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


int		sdl2gdk_key( int sdl_key );
int		gdk2sdl_key( int gdk_key );
gchar		*sdlkey_by_id( int sdl_key );
gchar		*sdlmod_by_id( int sdk_mod );
gchar		*gdkkey_by_id( int gdk_key );

gboolean	key_snooper(GtkWidget *w, GdkEventKey *event, gpointer user_data);

#endif
