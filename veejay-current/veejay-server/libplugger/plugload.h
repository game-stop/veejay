#ifndef PLUGINLOADER_
#define PLUGINLOADER_
/*
 * Copyright (C) 2002-2006 Niels Elburg <nwelburg@gmail.com>
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
	veejay plugin loader
        *	library to wrap up all kinds of "plugin standards"
 */
void	plug_sys_free(void);
void	plug_sys_init( int fmt, int w, int h );
void	plug_sys_set_palette( int palette );
int	plug_sys_detect_plugins(void);
int 	plug_get_num_output_channels(int id);
int	plug_get_num_input_channels(int id);
int	plug_get_num_parameters(int id);
char	*plug_get_name( int fx_id );
int	plug_get_fx_id_by_name( const char *name );
int	plug_get_num_input_channels( int fx_id );
int	plug_set_param_from_str( void *plugin , int p, const char *str, void *fx_values );
void	*plug_activate( int fx_id );
void	plug_deactivate( void *instance );
void	plug_push_frame( void *instance, int out, int seq_num, void *frame );
void	plug_process( void *instance, double timecode );
void	plug_get_defaults( void *instance, void *fx_values );
void	plug_set_parameter( void *instance, int seq_num, int n_elements,void *value );
void	plug_set_parameters( void *instance, int n_arg, void *darg );
int	plug_clone_from_output_parameters( void *instance, void *fx_values );
char	*plug_get_osc_format(void *fx_instance, int p);
//@ see generic_osc_cb_f in defs.h
void	plug_build_name_space( int fx_id, void *fx_instance, void *data, int entry_id, int sample_id,
	       void(*cb)(void *ud, void *p, void *v), void *cb_data	);
char 	*plug_get_osc_path_parameter(void *instance, int k);
int *plug_find_all_generator_plugins( int *total );
void	plug_clear_namespace( void *fx_instance, void *data );
int	plug_find_generator_plugins(int *total, int seq );
void	*plug_get( int fd_id );
vj_effect *plug_get_plugin( int fx_id );
void	*plug_get_by_so_name( char *soname );
int	plug_get_idx_by_name( char *name);
int 	plug_get_idx_by_so_name( char *soname );
int	plug_is_frei0r( void *instance );
#endif
