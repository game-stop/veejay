/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nelburg@looze.net> 
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

#ifndef VEEJAY_PLUGIN_H
#define VEEJAY_PLUGIN_H

#include <stdint.h>

#define VJPLUG_VIDO_DRIVER 1
#define VJPLUG_NORMAL 0
#define VJPLUG_VIDI_DRIVER 2

typedef struct 
{
        char    *name;                                                  // name of plugin
        char    *help;                                                  // help to display
	int	priority;
	int	plugin_type;
} VJPluginInfo;

typedef struct VJFrame_t 
{
        uint8_t *data[3];	// pixel data
        int     uv_len;		// U/V length
        int     len;		// Y length
        int     uv_width;	// U/V width
        int     uv_height;	// U/V height
        int     shift_v;	// shift vertical 
        int     shift_h;	// shift horizontal
} VJFrame;

typedef struct VJFrameInfo_t
{
        int width;		// width of project file
        int height;		// height of project file
	float fps;		// video frames per second
	int64_t timecode;	// not yet used
} VJFrameInfo;

enum
{
	VEEJAY_MSG_INFO = 2,
	VEEJAY_MSG_WARNING = 1,
	VEEJAY_MSG_ERROR = 0,
	VEEJAY_MSG_PRINT = 3,
	VEEJAY_MSG_DEBUG = 4,
};

// support function for parsing events
extern int	OptionParse(char *format, void *dst, const char *needle, const char *args);
extern void	veejay_msg( int type, const char format[] , ... );

#endif
