/* 
 * Linux VeeJay
 *
 * Copyright(C)2002-2004 Niels Elburg <nwelburg@gmail.com>
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

#ifndef _VEEJAY_PLUG_H
#define _VEEJAY_PLUG_H
#include <libvje/vje.h>
#define VJPLUG_VIDO_DRIVER 1
#define VJPLUG_NORMAL 0
#define VJPLUG_VIDI_DRIVER 2

#define VJ_EFFECT_ERROR -1024
#define VJ_EFFECT_DESCR_LEN 200;

#define ARRAY_SIZE(buf) (\
 (int) ( sizeof(buf[0]) / sizeof(uint8_t) ) + \
 (int) ( sizeof(buf[1]) / sizeof(uint8_t) ) +\
 (int) ( sizeof(buf[2]) / sizeof(uint8_t) ) )

/* VJPluginInfo
  this struct describeds the plugin and its events
*/
typedef struct 
{
	char	*name;							// name of plugin
	char	*help;							// help to display
	int	is_sink;
	int	is_src;
	int	plugin_type;
} VJPluginInfo;

#endif
