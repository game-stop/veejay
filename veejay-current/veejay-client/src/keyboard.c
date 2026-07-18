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
#include <string.h>
#include <config.h>
#include <gtk/gtk.h>
#ifdef HAVE_SDL
#include <gdk/gdkkeysyms.h>
#include <gdk/gdktypes.h>
#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_scancode.h>
#include "keyboard.h"

typedef enum {
    VIMS_MOD_NONE     = 0x0000,
    VIMS_MOD_ALT      = 0x0001,
    VIMS_MOD_CTRL     = 0x0002,
    VIMS_MOD_SHIFT    = 0x0004,
    VIMS_MOD_CAPSLOCK = 0x0008,
} KEYMod;

typedef struct {
    int vims_mod;
    int gdk_mod;
    const gchar *title;
} ModifierTranslation;

typedef struct {
    guint gdk_sym;
    SDL_Scancode sdl_scancode;
    const gchar *title;
} KeyTranslation;

typedef struct {
    guint16 hardware_keycode;
    SDL_Scancode sdl_scancode;
} HardwareTranslation;

static const ModifierTranslation modifier_translation_table[] = {
    { VIMS_MOD_NONE, 0, "none" },
    { VIMS_MOD_ALT, GDK_MOD1_MASK, "alt" },
    { VIMS_MOD_CTRL, GDK_CONTROL_MASK, "ctrl" },
    { VIMS_MOD_CTRL | VIMS_MOD_ALT,
      GDK_CONTROL_MASK | GDK_MOD1_MASK, "ctrl+alt" },
    { VIMS_MOD_SHIFT, GDK_SHIFT_MASK, "shift" },
    { VIMS_MOD_ALT | VIMS_MOD_SHIFT,
      GDK_MOD1_MASK | GDK_SHIFT_MASK, "alt+shift" },
    { VIMS_MOD_CTRL | VIMS_MOD_SHIFT,
      GDK_CONTROL_MASK | GDK_SHIFT_MASK, "ctrl+shift" },
    { VIMS_MOD_CTRL | VIMS_MOD_ALT | VIMS_MOD_SHIFT,
      GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK,
      "ctrl+alt+shift" },
    { VIMS_MOD_CAPSLOCK, GDK_LOCK_MASK, "capslock" },
    { VIMS_MOD_ALT | VIMS_MOD_CAPSLOCK,
      GDK_MOD1_MASK | GDK_LOCK_MASK, "alt+capslock" },
    { VIMS_MOD_CTRL | VIMS_MOD_CAPSLOCK,
      GDK_CONTROL_MASK | GDK_LOCK_MASK, "ctrl+capslock" },
    { VIMS_MOD_CTRL | VIMS_MOD_ALT | VIMS_MOD_CAPSLOCK,
      GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_LOCK_MASK,
      "ctrl+alt+capslock" },
    { VIMS_MOD_SHIFT | VIMS_MOD_CAPSLOCK,
      GDK_SHIFT_MASK | GDK_LOCK_MASK, "shift+capslock" },
    { VIMS_MOD_ALT | VIMS_MOD_SHIFT | VIMS_MOD_CAPSLOCK,
      GDK_MOD1_MASK | GDK_SHIFT_MASK | GDK_LOCK_MASK,
      "alt+shift+capslock" },
    { VIMS_MOD_CTRL | VIMS_MOD_SHIFT | VIMS_MOD_CAPSLOCK,
      GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_LOCK_MASK,
      "ctrl+shift+capslock" },
    { VIMS_MOD_CTRL | VIMS_MOD_ALT | VIMS_MOD_SHIFT | VIMS_MOD_CAPSLOCK,
      GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_SHIFT_MASK | GDK_LOCK_MASK,
      "ctrl+alt+shift+capslock" },
    { 0, 0, NULL }
};

/* The backend stores SDL_Scancode values, not SDL_Keycode values. */
static const KeyTranslation key_translation_table[] = {
    { GDK_KEY_Escape,       SDL_SCANCODE_ESCAPE,       "Escape" },
    { GDK_KEY_1,            SDL_SCANCODE_1,            "1" },
    { GDK_KEY_2,            SDL_SCANCODE_2,            "2" },
    { GDK_KEY_3,            SDL_SCANCODE_3,            "3" },
    { GDK_KEY_4,            SDL_SCANCODE_4,            "4" },
    { GDK_KEY_5,            SDL_SCANCODE_5,            "5" },
    { GDK_KEY_6,            SDL_SCANCODE_6,            "6" },
    { GDK_KEY_7,            SDL_SCANCODE_7,            "7" },
    { GDK_KEY_8,            SDL_SCANCODE_8,            "8" },
    { GDK_KEY_9,            SDL_SCANCODE_9,            "9" },
    { GDK_KEY_0,            SDL_SCANCODE_0,            "0" },
    { GDK_KEY_minus,        SDL_SCANCODE_MINUS,        "-" },
    { GDK_KEY_equal,        SDL_SCANCODE_EQUALS,       "=" },
    { GDK_KEY_BackSpace,    SDL_SCANCODE_BACKSPACE,    "Backspace" },
    { GDK_KEY_Tab,          SDL_SCANCODE_TAB,          "Tab" },
    { GDK_KEY_q,            SDL_SCANCODE_Q,            "Q" },
    { GDK_KEY_w,            SDL_SCANCODE_W,            "W" },
    { GDK_KEY_e,            SDL_SCANCODE_E,            "E" },
    { GDK_KEY_r,            SDL_SCANCODE_R,            "R" },
    { GDK_KEY_t,            SDL_SCANCODE_T,            "T" },
    { GDK_KEY_y,            SDL_SCANCODE_Y,            "Y" },
    { GDK_KEY_u,            SDL_SCANCODE_U,            "U" },
    { GDK_KEY_i,            SDL_SCANCODE_I,            "I" },
    { GDK_KEY_o,            SDL_SCANCODE_O,            "O" },
    { GDK_KEY_p,            SDL_SCANCODE_P,            "P" },
    { GDK_KEY_bracketleft,  SDL_SCANCODE_LEFTBRACKET,  "[" },
    { GDK_KEY_bracketright, SDL_SCANCODE_RIGHTBRACKET, "]" },
    { GDK_KEY_Return,       SDL_SCANCODE_RETURN,       "Enter" },
    { GDK_KEY_ISO_Enter,    SDL_SCANCODE_RETURN,       "Enter" },
    { GDK_KEY_3270_Enter,   SDL_SCANCODE_RETURN,       "Enter" },
    { GDK_KEY_a,            SDL_SCANCODE_A,            "A" },
    { GDK_KEY_s,            SDL_SCANCODE_S,            "S" },
    { GDK_KEY_d,            SDL_SCANCODE_D,            "D" },
    { GDK_KEY_f,            SDL_SCANCODE_F,            "F" },
    { GDK_KEY_g,            SDL_SCANCODE_G,            "G" },
    { GDK_KEY_h,            SDL_SCANCODE_H,            "H" },
    { GDK_KEY_j,            SDL_SCANCODE_J,            "J" },
    { GDK_KEY_k,            SDL_SCANCODE_K,            "K" },
    { GDK_KEY_l,            SDL_SCANCODE_L,            "L" },
    { GDK_KEY_semicolon,    SDL_SCANCODE_SEMICOLON,    ";" },
    { GDK_KEY_apostrophe,   SDL_SCANCODE_APOSTROPHE,   "'" },
    { GDK_KEY_grave,        SDL_SCANCODE_GRAVE,        "`" },
    { GDK_KEY_backslash,    SDL_SCANCODE_BACKSLASH,    "\\" },
    { GDK_KEY_z,            SDL_SCANCODE_Z,            "Z" },
    { GDK_KEY_x,            SDL_SCANCODE_X,            "X" },
    { GDK_KEY_c,            SDL_SCANCODE_C,            "C" },
    { GDK_KEY_v,            SDL_SCANCODE_V,            "V" },
    { GDK_KEY_b,            SDL_SCANCODE_B,            "B" },
    { GDK_KEY_n,            SDL_SCANCODE_N,            "N" },
    { GDK_KEY_m,            SDL_SCANCODE_M,            "M" },
    { GDK_KEY_comma,        SDL_SCANCODE_COMMA,        "," },
    { GDK_KEY_period,       SDL_SCANCODE_PERIOD,       "." },
    { GDK_KEY_slash,        SDL_SCANCODE_SLASH,        "/" },
    { GDK_KEY_space,        SDL_SCANCODE_SPACE,        "Space" },
    { GDK_KEY_F1,           SDL_SCANCODE_F1,           "F1" },
    { GDK_KEY_F2,           SDL_SCANCODE_F2,           "F2" },
    { GDK_KEY_F3,           SDL_SCANCODE_F3,           "F3" },
    { GDK_KEY_F4,           SDL_SCANCODE_F4,           "F4" },
    { GDK_KEY_F5,           SDL_SCANCODE_F5,           "F5" },
    { GDK_KEY_F6,           SDL_SCANCODE_F6,           "F6" },
    { GDK_KEY_F7,           SDL_SCANCODE_F7,           "F7" },
    { GDK_KEY_F8,           SDL_SCANCODE_F8,           "F8" },
    { GDK_KEY_F9,           SDL_SCANCODE_F9,           "F9" },
    { GDK_KEY_F10,          SDL_SCANCODE_F10,          "F10" },
    { GDK_KEY_F11,          SDL_SCANCODE_F11,          "F11" },
    { GDK_KEY_F12,          SDL_SCANCODE_F12,          "F12" },
    { GDK_KEY_Print,        SDL_SCANCODE_PRINTSCREEN,  "Print Screen" },
    { GDK_KEY_Scroll_Lock,  SDL_SCANCODE_SCROLLLOCK,   "Scroll Lock" },
    { GDK_KEY_Pause,        SDL_SCANCODE_PAUSE,        "Pause" },
    { GDK_KEY_Insert,       SDL_SCANCODE_INSERT,       "Insert" },
    { GDK_KEY_Home,         SDL_SCANCODE_HOME,         "Home" },
    { GDK_KEY_Page_Up,      SDL_SCANCODE_PAGEUP,       "Page Up" },
    { GDK_KEY_Delete,       SDL_SCANCODE_DELETE,       "Delete" },
    { GDK_KEY_End,          SDL_SCANCODE_END,          "End" },
    { GDK_KEY_Page_Down,    SDL_SCANCODE_PAGEDOWN,     "Page Down" },
    { GDK_KEY_Right,        SDL_SCANCODE_RIGHT,        "Right" },
    { GDK_KEY_Left,         SDL_SCANCODE_LEFT,         "Left" },
    { GDK_KEY_Down,         SDL_SCANCODE_DOWN,         "Down" },
    { GDK_KEY_Up,           SDL_SCANCODE_UP,           "Up" },
    { GDK_KEY_Num_Lock,     SDL_SCANCODE_NUMLOCKCLEAR, "Num Lock" },
    { GDK_KEY_KP_Divide,    SDL_SCANCODE_KP_DIVIDE,    "Keypad /" },
    { GDK_KEY_KP_Multiply,  SDL_SCANCODE_KP_MULTIPLY,  "Keypad *" },
    { GDK_KEY_KP_Subtract,  SDL_SCANCODE_KP_MINUS,     "Keypad -" },
    { GDK_KEY_KP_Add,       SDL_SCANCODE_KP_PLUS,      "Keypad +" },
    { GDK_KEY_KP_Enter,     SDL_SCANCODE_KP_ENTER,     "Keypad Enter" },
    { GDK_KEY_KP_1,         SDL_SCANCODE_KP_1,         "Keypad 1" },
    { GDK_KEY_KP_2,         SDL_SCANCODE_KP_2,         "Keypad 2" },
    { GDK_KEY_KP_3,         SDL_SCANCODE_KP_3,         "Keypad 3" },
    { GDK_KEY_KP_4,         SDL_SCANCODE_KP_4,         "Keypad 4" },
    { GDK_KEY_KP_5,         SDL_SCANCODE_KP_5,         "Keypad 5" },
    { GDK_KEY_KP_6,         SDL_SCANCODE_KP_6,         "Keypad 6" },
    { GDK_KEY_KP_7,         SDL_SCANCODE_KP_7,         "Keypad 7" },
    { GDK_KEY_KP_8,         SDL_SCANCODE_KP_8,         "Keypad 8" },
    { GDK_KEY_KP_9,         SDL_SCANCODE_KP_9,         "Keypad 9" },
    { GDK_KEY_KP_0,         SDL_SCANCODE_KP_0,         "Keypad 0" },
    { GDK_KEY_KP_Decimal,   SDL_SCANCODE_KP_PERIOD,    "Keypad ." },
    { GDK_KEY_KP_Equal,     SDL_SCANCODE_KP_EQUALS,    "Keypad =" },
    { 0,                    SDL_SCANCODE_UNKNOWN,       NULL }
};

/* Standard Linux evdev/XKB key positions (evdev code + 8). */
static const HardwareTranslation hardware_translation_table[] = {
    { 9, SDL_SCANCODE_ESCAPE },
    { 10, SDL_SCANCODE_1 }, { 11, SDL_SCANCODE_2 }, { 12, SDL_SCANCODE_3 },
    { 13, SDL_SCANCODE_4 }, { 14, SDL_SCANCODE_5 }, { 15, SDL_SCANCODE_6 },
    { 16, SDL_SCANCODE_7 }, { 17, SDL_SCANCODE_8 }, { 18, SDL_SCANCODE_9 },
    { 19, SDL_SCANCODE_0 }, { 20, SDL_SCANCODE_MINUS }, { 21, SDL_SCANCODE_EQUALS },
    { 22, SDL_SCANCODE_BACKSPACE }, { 23, SDL_SCANCODE_TAB },
    { 24, SDL_SCANCODE_Q }, { 25, SDL_SCANCODE_W }, { 26, SDL_SCANCODE_E },
    { 27, SDL_SCANCODE_R }, { 28, SDL_SCANCODE_T }, { 29, SDL_SCANCODE_Y },
    { 30, SDL_SCANCODE_U }, { 31, SDL_SCANCODE_I }, { 32, SDL_SCANCODE_O },
    { 33, SDL_SCANCODE_P }, { 34, SDL_SCANCODE_LEFTBRACKET },
    { 35, SDL_SCANCODE_RIGHTBRACKET }, { 36, SDL_SCANCODE_RETURN },
    { 37, SDL_SCANCODE_LCTRL },
    { 38, SDL_SCANCODE_A }, { 39, SDL_SCANCODE_S }, { 40, SDL_SCANCODE_D },
    { 41, SDL_SCANCODE_F }, { 42, SDL_SCANCODE_G }, { 43, SDL_SCANCODE_H },
    { 44, SDL_SCANCODE_J }, { 45, SDL_SCANCODE_K }, { 46, SDL_SCANCODE_L },
    { 47, SDL_SCANCODE_SEMICOLON }, { 48, SDL_SCANCODE_APOSTROPHE },
    { 49, SDL_SCANCODE_GRAVE }, { 50, SDL_SCANCODE_LSHIFT },
    { 51, SDL_SCANCODE_BACKSLASH },
    { 52, SDL_SCANCODE_Z }, { 53, SDL_SCANCODE_X }, { 54, SDL_SCANCODE_C },
    { 55, SDL_SCANCODE_V }, { 56, SDL_SCANCODE_B }, { 57, SDL_SCANCODE_N },
    { 58, SDL_SCANCODE_M }, { 59, SDL_SCANCODE_COMMA },
    { 60, SDL_SCANCODE_PERIOD }, { 61, SDL_SCANCODE_SLASH },
    { 62, SDL_SCANCODE_RSHIFT }, { 63, SDL_SCANCODE_KP_MULTIPLY },
    { 64, SDL_SCANCODE_LALT }, { 65, SDL_SCANCODE_SPACE },
    { 66, SDL_SCANCODE_CAPSLOCK },
    { 67, SDL_SCANCODE_F1 }, { 68, SDL_SCANCODE_F2 }, { 69, SDL_SCANCODE_F3 },
    { 70, SDL_SCANCODE_F4 }, { 71, SDL_SCANCODE_F5 }, { 72, SDL_SCANCODE_F6 },
    { 73, SDL_SCANCODE_F7 }, { 74, SDL_SCANCODE_F8 }, { 75, SDL_SCANCODE_F9 },
    { 76, SDL_SCANCODE_F10 }, { 77, SDL_SCANCODE_NUMLOCKCLEAR },
    { 78, SDL_SCANCODE_SCROLLLOCK }, { 79, SDL_SCANCODE_KP_7 },
    { 80, SDL_SCANCODE_KP_8 }, { 81, SDL_SCANCODE_KP_9 },
    { 82, SDL_SCANCODE_KP_MINUS }, { 83, SDL_SCANCODE_KP_4 },
    { 84, SDL_SCANCODE_KP_5 }, { 85, SDL_SCANCODE_KP_6 },
    { 86, SDL_SCANCODE_KP_PLUS }, { 87, SDL_SCANCODE_KP_1 },
    { 88, SDL_SCANCODE_KP_2 }, { 89, SDL_SCANCODE_KP_3 },
    { 90, SDL_SCANCODE_KP_0 }, { 91, SDL_SCANCODE_KP_PERIOD },
    { 95, SDL_SCANCODE_F11 }, { 96, SDL_SCANCODE_F12 },
    { 104, SDL_SCANCODE_KP_ENTER }, { 105, SDL_SCANCODE_RCTRL },
    { 106, SDL_SCANCODE_KP_DIVIDE },
    { 107, SDL_SCANCODE_PRINTSCREEN }, { 108, SDL_SCANCODE_RALT },
    { 110, SDL_SCANCODE_HOME },
    { 111, SDL_SCANCODE_UP }, { 112, SDL_SCANCODE_PAGEUP },
    { 113, SDL_SCANCODE_LEFT }, { 114, SDL_SCANCODE_RIGHT },
    { 115, SDL_SCANCODE_END }, { 116, SDL_SCANCODE_DOWN },
    { 117, SDL_SCANCODE_PAGEDOWN }, { 118, SDL_SCANCODE_INSERT },
    { 119, SDL_SCANCODE_DELETE }, { 127, SDL_SCANCODE_PAUSE },
    { 133, SDL_SCANCODE_LGUI }, { 134, SDL_SCANCODE_RGUI },
    { 135, SDL_SCANCODE_APPLICATION },
    { 0, SDL_SCANCODE_UNKNOWN }
};

static SDL_Scancode hardware_to_scancode(const GdkEventKey *event)
{
    guint keycode;

    if(!event || event->hardware_keycode == 0)
        return SDL_SCANCODE_UNKNOWN;

    keycode = event->hardware_keycode;

    for(int i = 0; hardware_translation_table[i].hardware_keycode != 0; i++)
        if(hardware_translation_table[i].hardware_keycode == keycode)
            return hardware_translation_table[i].sdl_scancode;

    return SDL_SCANCODE_UNKNOWN;
}

static guint unshifted_keyval(const GdkEventKey *event)
{
    GdkKeymap *keymap;
    guint keyval = event ? event->keyval : 0;
    GdkModifierType state;

    if(!event)
        return 0;

    keymap = gdk_keymap_get_for_display(gdk_display_get_default());
    state = event->state & ~(GDK_SHIFT_MASK | GDK_LOCK_MASK |
                             GDK_CONTROL_MASK | GDK_MOD1_MASK |
                             GDK_SUPER_MASK | GDK_META_MASK | GDK_HYPER_MASK);
    gdk_keymap_translate_keyboard_state(keymap,
                                        event->hardware_keycode,
                                        state,
                                        event->group,
                                        &keyval,
                                        NULL,
                                        NULL,
                                        NULL);
    return gdk_keyval_to_lower(keyval);
}

int sdl2gdk_key(int sdl_key)
{
    for(int i = 0; key_translation_table[i].title != NULL; i++)
        if(sdl_key == (int)key_translation_table[i].sdl_scancode)
            return (int)key_translation_table[i].gdk_sym;
    return 0;
}

int gdk2sdl_key(int gdk_key)
{
    guint keyval = gdk_keyval_to_lower((guint)gdk_key);

    for(int i = 0; key_translation_table[i].title != NULL; i++)
        if(keyval == key_translation_table[i].gdk_sym)
            return (int)key_translation_table[i].sdl_scancode;
    return 0;
}

int gdk_event_key_to_sdl(const GdkEventKey *event, int *sdl_key, int *vims_mod)
{
    SDL_Scancode scancode;

    if(!event)
        return 0;

    scancode = hardware_to_scancode(event);
    if(scancode == SDL_SCANCODE_UNKNOWN)
        scancode = (SDL_Scancode)gdk2sdl_key((int)unshifted_keyval(event));
    if(scancode == SDL_SCANCODE_UNKNOWN)
        return 0;

    if(sdl_key)
        *sdl_key = (int)scancode;
    if(vims_mod)
        *vims_mod = gdk2sdl_mod((int)event->state);
    return 1;
}

int gdk2sdl_mod(int gdk_mod)
{
    int result = VIMS_MOD_NONE;

    if(gdk_mod & GDK_MOD1_MASK)
        result |= VIMS_MOD_ALT;
    if(gdk_mod & GDK_CONTROL_MASK)
        result |= VIMS_MOD_CTRL;
    if(gdk_mod & GDK_SHIFT_MASK)
        result |= VIMS_MOD_SHIFT;
    if(gdk_mod & GDK_LOCK_MASK)
        result |= VIMS_MOD_CAPSLOCK;
    return result;
}

int sdlmod_by_name(gchar *name)
{
    if(!name)
        return 0;

    for(int i = 0; modifier_translation_table[i].title != NULL; i++)
        if(g_ascii_strcasecmp(name, modifier_translation_table[i].title) == 0)
            return modifier_translation_table[i].vims_mod;
    return 0;
}

int sdlkey_by_name(gchar *name)
{
    if(!name)
        return 0;

    for(int i = 0; key_translation_table[i].title != NULL; i++)
        if(g_ascii_strcasecmp(name, key_translation_table[i].title) == 0)
            return (int)key_translation_table[i].sdl_scancode;

    return (int)SDL_GetScancodeFromName(name);
}

gchar *sdlkey_by_id(int sdl_key)
{
    static gchar unknown[32];
    const char *name;

    if(sdl_key <= SDL_SCANCODE_UNKNOWN || sdl_key >= SDL_NUM_SCANCODES) {
        g_snprintf(unknown, sizeof(unknown), "scancode %d", sdl_key);
        return unknown;
    }

    name = SDL_GetScancodeName((SDL_Scancode)sdl_key);
    if(name && *name)
        return (gchar*)name;

    for(int i = 0; key_translation_table[i].title != NULL; i++)
        if(sdl_key == (int)key_translation_table[i].sdl_scancode)
            return (gchar*)key_translation_table[i].title;

    g_snprintf(unknown, sizeof(unknown), "scancode %d", sdl_key);
    return unknown;
}

gchar *sdlmod_by_id(int vims_mod)
{
    static gchar unknown[32];

    for(int i = 0; modifier_translation_table[i].title != NULL; i++)
        if(vims_mod == modifier_translation_table[i].vims_mod)
            return (gchar*)modifier_translation_table[i].title;

    g_snprintf(unknown, sizeof(unknown), "modifier %d", vims_mod);
    return unknown;
}

gchar *gdkmod_by_id(int gdk_mod)
{
    for(int i = 0; modifier_translation_table[i].title != NULL; i++)
        if(gdk_mod == modifier_translation_table[i].gdk_mod)
            return (gchar*)modifier_translation_table[i].title;
    return NULL;
}

gchar *gdkkey_by_id(int gdk_key)
{
    guint keyval = gdk_keyval_to_lower((guint)gdk_key);

    for(int i = 0; key_translation_table[i].title != NULL; i++)
        if(keyval == key_translation_table[i].gdk_sym)
            return (gchar*)key_translation_table[i].title;
    return NULL;
}

gboolean key_snooper(GtkWidget *w, GdkEventKey *event, gpointer user_data)
{
    (void)w;
    (void)event;
    (void)user_data;
    return FALSE;
}
#endif
