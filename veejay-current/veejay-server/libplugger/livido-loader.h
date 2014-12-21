/* veejay - Linux VeeJay - libplugger utility
 *           (C) 2010      Niels Elburg <nwelburg@gmail.com> ported from veejay-ng
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
#ifndef LIVIDO_LOADER_H
#define LIVIDO_LOADER_H

int		lvd_livido_palette( int v );

void	*deal_with_livido(void *handle, const char *name);

void	*livido_plug_init( void *plugin, int w, int h, int pixfmt );

void	 livido_plug_deinit( void *plugin );

void	livido_plug_free( void *plugin );

void	livido_plug_process( void *plugin, double timecode );

//@ set preferred palette
void	livido_set_pref_palette( int pref_palette );

int	livido_plug_inplace( void *instance );

void	livido_push_channel( void *instance,int n, int dir, VJFrame *frame );
#define livido_push_input_channel(instance,n,frame) livido_push_channel( instance, "in_channels", n , frame )
#define livido_push_output_channel(instance,n,frame) livido_push_channel( instance, "out_channels",n, frame )

//@ get default values
void	livido_plug_retrieve_values( void *instance, void *fx_values );

//@ takes another port and clones into value
void	livido_clone_parameter( void *instance, int seq, void *fx_value_port );

void	livido_reverse_clone_parameter( void *instance, int seq, void *fx_value_port );

void	livido_set_parameter( void *instance, int seq, void *value );

void	livido_set_parameters_scaled( void *plugin, int *args ); 

void	livido_exit( void );

int	livido_plug_read_output_parameters( void *instance, void *fx_values );

char	*livido_describe_parameter_format( void *instance, int p );

int	livido_plug_build_namespace( void *plugin_template , int entry_id, void *fx_instance , void *data, int sam,
		generic_osc_cb_f cbf, void *cb_data);

char	*livido_describe_parameter_format_osc( void *instance, int p );

void	livido_plug_free_namespace( void *fx_instance , void *data );

void	*livido_get_name_space( void *instance );

// utility
int	livido_plug_get_coord_parameter_as_dbl( void *fx_instance,const char *key, int k, double *res_x, double *res_y );
int	livido_plug_parameter_get_range_dbl( void *fx_instance,const char *key, int k, double *min, double *max, int *dkind );
int     livido_set_parameter_from_string( void *instance, int p, const char *str, void *fx_values );
int	livido_plug_get_number_parameter_as_dbl( void *fx_instance,const char *key, int k, double *res );
int	livido_plug_get_index_parameter_as_dbl( void *fx_instance, const char *key,int k, double *res );
#endif
