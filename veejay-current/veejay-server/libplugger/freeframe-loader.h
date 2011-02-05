#ifndef FREEFRAME_LOADER
#define FREEFRAME_LOADER
void* 	deal_with_ff( void *handle, char *name, int width, int height );
 

int	freeframe_plug_process( void *plugin, double timecode );


void	freeframe_push_channel( void *instance, int dir,int n, VJFrame *frame );


void	freeframe_plug_free( void *plugin );

void	freeframe_plug_deinit( void *plugin );


void *freeframe_plug_init( void *plugin, int w, int h );

void	freeframe_plug_retrieve_current_values( void *instance, void *fx_values );

void	freeframe_plug_retrieve_default_values( void *instance, void *fx_values );

void	freeframe_destroy( );

void	freeframe_plug_param_f( void *plugin, int seq_no, void *dargs );

#endif
