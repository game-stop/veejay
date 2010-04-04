/*
 * Linux VeeJay
 * V4l1 driver , classic stuff
 * Copyright(C)2008 Niels Elburg <nwelburg@gmail.com>
 * uses v4lutils - utility library for Video4Linux
 *      Copyright (C) 2001-2002 FUKUCHI Kentaro

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



#ifndef KENTARO_V4L_UTILS
#define KENTARO_V4L_UTILS
int	v4lvideo_is_paused(void *vv );
void	v4lvideo_set_paused(void *vv, int pause);
int	v4lvideo_templ_get_norm( const char *name );
int	v4lvideo_templ_getfreq( const char *name );
int	v4lvideo_templ_num_devices();
int	v4lvideo_templ_get_palette( int p );
char *v4lvideo_templ_get_norm_str(int id );

char	**v4lvideo_templ_get_devices(int *num);

void	*v4lvideo_init( char* file, int channel, int norm, int freq, int w, int h, int palette );
void	v4lvideo_destroy( void *vv );

int	v4lvideo_grabstart( void *vv );

int	v4lvideo_grabstop( void *vv );
int	v4lvideo_syncframe(void *vv);
int	v4lvideo_grabframe( void *vv );
//uint8_t *v4lvideo_getaddress( void *vv );
int	v4lvideo_copy_framebuffer_to( void *vv, uint8_t *dstY, uint8_t *dstU, uint8_t *dstV );

int	v4lvideo_setfreq( void *vv, int f );

int	v4lvideo_get_brightness( void *vv ); 
int	v4lvideo_get_hue( void *vv ); 
int	v4lvideo_get_colour( void *vv ); 
int	v4lvideo_get_contrast( void *vv ); 
int	v4lvideo_get_white(void *vv);
void	v4lvideo_set_brightness( void *vv, int x );
void	v4lvideo_set_hue( void *vv, int x );
void	v4lvideo_set_colour( void *vv, int x );
void	v4lvideo_set_constrast( void *vv, int x );
void	v4lvideo_set_white(void *vv, int x );

int	v4lvideo_change_channel( void *vv, int channel );

// veejay specific
void	v4lvideo_set_composite_status( void *vv, int status );
int	v4lvideo_get_composite_status( void *vv );


#endif

