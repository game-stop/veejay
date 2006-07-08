#ifndef FREEFRAME_LOADER
#define FREEFRAME_LOADER
void* 	deal_with_ff( void *handle, char *name );
 
int	freeframe_plug_init( void *plugin , int w, int h );
void	freeframe_plug_deinit( void *plugin );
void	freeframe_plug_free( void *plugin );
int	freeframe_plug_process( void *plugin, void *in );
void	freeframe_plug_process_ext( void *plugin, void *in0, void *in1, void *out);
void	freeframe_plug_control( void *plugin, int *args );

#endif
