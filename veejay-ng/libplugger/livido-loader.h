#ifndef LIVIDO_LOADER_H
#define LIVIDO_LOADER_H

void	*deal_with_livido(void *handle, const char *name);

void	*livido_plug_init( void *plugin, int w, int h );

void	 livido_plug_deinit( void *plugin );

void	livido_plug_free( void *plugin );

void	livido_plug_process( void *plugin, double timecode );

//@ set preferred palette
void	livido_set_pref_palette( int pref_palette );

int	livido_plug_inplace( void *instance );

void	livido_push_channel( void *instance,const char *key, int n, VJFrame *frame );
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

void	livido_plug_read_output_parameters( void *instance, void *fx_values );

char	*livido_describe_parameter_format( void *instance, int p );

int	livido_plug_build_namespace( void *plugin_template , int entry_id, void *fx_instance , void *data, int sam);

char	*livido_describe_parameter_format_osc( void *instance, int p );

void	livido_plug_free_namespace( void *fx_instance , void *data );

void	*livido_get_name_space( void *instance );
#endif
