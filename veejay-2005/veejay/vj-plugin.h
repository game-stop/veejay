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


	This is the Interface description for Veejay Plugins 

	Features:
		- Pull mechanism
		- Push mechanism
		- Loophole/Feedback mechanism

	A Veejay Plugin can act as a Sink and/or Source ,
	
	Use Sink if you want to play for example to SDL ( see plugin/sdl.c )
	Use Source if you want to play from, for example V4L ( see plugin/v4l.c )
	

	TODO:
		Stream <-> Pull Plugin

*/

#ifndef _VEEJAY_PLUGIN_H
#define _VEEJAY_PLUGIN_H
#include <veejay/vj-plug.h>

typedef int (Fplugin_init)(void **ctxp);
typedef Fplugin_init *vj_plugin_init;
extern vj_plugin_init Init;

/* function Process
  this function receives your plugin's context, Frame Information and a structure containing a picture

  It should return 0 on error,  1 on success
*/
typedef int (Fplugin_process)(void *ctx, void *info, void *picture );
typedef Fplugin_process *vj_plugin_process;
extern vj_plugin_process Process;

/* function Pull
  this function receives your plugin's context, Frame Information and a structure containing a picture
  You should write something to this picture, it is used as a Stream in veejay and is called
  periodically.

  It should return 0 on error,  1 on success
*/
typedef int (Fplugin_pull)(void *ctx, void *info, void *dst_picture );
typedef Fplugin_pull *vj_plugin_pull;
extern vj_plugin_pull Pull;

/* function Free  
  this function receives a pointer to your plugin's context, it should contain a procedure for
  freeing the memory your plugin may use. This function is optional

*/
typedef void (Fplugin_free)(void *ctx);
typedef Fplugin_free *vj_plugin_free;
extern vj_plugin_free Free;

/* function Event
  this function receives a pointer to your plugin's context,
  a number describing which event to execute and its arguments.
*/ 
typedef int (Fplugins_event)(void *context, const char *tokens );
typedef Fplugins_event *vj_plugin_event;
extern vj_plugin_event Event;


/* function Info
  this function returns the structure VJPluginInfo, describing your plugin to veejay
  if left to NULL, veejay will not try to access any of your plugin's events.

*/

typedef VJPluginInfo* (Fplugins_info)(void *context);
typedef Fplugins_info *vj_plugin_info;
extern vj_plugin_info Info;


extern void		plugins_allocate(void);

extern int		plugins_init(const char *name);

extern void		plugins_process_video_out(void *info, void *picture);
extern void		plugins_process_video_in(void *info, void *picture);
extern void		plugins_process(void *info, void *picture);
extern void		plugins_pull(void *info, void *picture);
extern void		plugins_free(const char *name);
extern void		plugins_event(const char *name, const char *tokens);
extern VJPluginInfo	*plugins_get_pluginfo(char *name);




#endif
