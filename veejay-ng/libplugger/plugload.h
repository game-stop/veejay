#ifndef PLUGINLOADER_
#define PLUGINLOADER_
/*
 * Copyright (C) 2002-2006 Niels Elburg <nelburg@looze.net>
 * 
 * This program is free software you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

/*
	video plugin loader library to wrap up all kinds of standards
 */

void	plug_sys_free(void);
void	plug_sys_init( int fmt, int w, int h );
void	plug_sys_set_palette( int palette );
int	plug_sys_detect_plugins(void);
char	*plug_get_name( int fx_id );
int	plug_get_fx_id_by_name( const char *name );
int	plug_get_num_input_channels( int fx_id );
int		plug_set_param_from_str( void *plugin , int p, const char *str, void *fx_values );
//@ initialize plugin
void	*plug_activate( int fx_id );
void	plug_deactivate( void *instance );
void	plug_push_frame( void *instance, int out, int seq_num, void *frame );
void	plug_process( void *instance );
void	plug_get_defaults( void *instance, void *fx_values );
void	plug_set_parameter( void *instance, int seq_num, int n_elements,void *value );
int	plug_inplace( void *instance );

#endif
