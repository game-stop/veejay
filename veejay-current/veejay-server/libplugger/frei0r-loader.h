#ifndef FREIORLOADER_H
#define FREIORLOADER_H

void* 	deal_with_fr( void *handle, char *name );
 
void frei0r_destroy();

void*	frei0r_plug_init( void *plugin , int w, int h,int pf );

void	frei0r_plug_deinit( void *plugin );

void	frei0r_plug_free( void *plugin );

int	frei0r_plug_process( void *plugin, void *in, void *out );

void	frei0r_plug_process_ext( void *plugin, void *in1, void *in2, void *out);

void	frei0r_plug_param_f( void *port, int num_args, int *dargs );

int	init_frei0r(int w, int h, int pf);
#endif
