#include <config.h>
#include <dlfcn.h>
#include <stdarg.h>
#include <include/libvevo.h>

static uint8_t *data[3];
int 	width = 10;
int 	height = 10;

void	out( int ntabs, const char format[], ... )
{
	char buf[1024];
	va_list args;
	va_start( args, format );
	vsnprintf( buf, sizeof(buf) - 1, format, args );
	if( ntabs > 0)
	{
		int i;
		for( i = 0; i < ntabs; i ++ )
			printf("\t");
	}
	printf("%s\n", buf);
	va_end(args);
}

static 	void	dump_plugin_info( vevo_instance_t *plugin , vevo_instance_templ_t *plug)
{
	int ni=0,no=0,pi=0,po=0;

	vevo_get_property( plugin->self, VEVOI_N_IN_PARAMETERS, &pi );
	vevo_get_property( plugin->self, VEVOI_N_OUT_PARAMETERS, &po );
	vevo_get_property( plugin->self, VEVOI_N_IN_CHANNELS, &ni );
	vevo_get_property( plugin->self, VEVOI_N_OUT_CHANNELS, &no );

	out(0,"Plugin Information:");
	out(1,"Name:   %s", plug->name );
	out(1,"Author: %s", plug->author );
	out(1,"Descr:  %s", plug->description );
	out(1,"Version:%s", plug->version );	
	out(1,"Input parameters : %d", pi );
	out(1,"Input channels   : %d", ni );
	out(1,"Output parameters: %d", no );
	out(1,"Output channels  : %d", po );

	out(0,"RUNNING PLUGIN:\n");
}

static	void	dump_host_info(void)
{
	int i ;
	out(0,"Host information:");
	for( i = 0; i < 10; i ++ )
	{
		out(1,"Channel %d memory Y=%p U=%p V=%p",
			i,
			data[0] + (i * 100),
			data[1] + (i * 100),
			data[2] + (i * 100)
		);
		out(2,"Width %d, Height %d", width,height );
	}
}

void	test_parameters( vevo_instance_t *plug )
{
	int i,pi;
	vevo_get_property( plug->self, VEVOI_N_IN_PARAMETERS, &pi );

	/* handle initialization of 'value' (from effect) */
	for( i = 0; i < pi ; i ++ )
	{
		if( vevo_property_assign_value( plug->in_params[i], VEVOP_VALUE, VEVOP_DEFAULT ) !=VEVO_ERR_SUCCESS)
		{
			printf("Unable to initialize parameter %d\n", i);
			exit(0);
		}
		else
		{
			if( vevo_find_property( plug->in_params[i], VEVOP_VALUE ) != 0 )
			{
				printf("cannot find VALUE\n");
				exit(0);
			}
		}
	}

}

vevo_instance_t *readyfy_plugin( vevo_instance_templ_t *tmpl )
{
	vevo_instance_t *plug = vevo_allocate_instance( tmpl );

	int i;
	int j;

	data[0] = (uint8_t*) malloc(sizeof(uint8_t) * 1000);
	data[1] = (uint8_t*) malloc(sizeof(uint8_t) * 1000);
	data[2] = (uint8_t*) malloc(sizeof(uint8_t) * 1000);

	memset(data[0], 16, 100);
	memset(data[1],128, 200);
	memset(data[2],128, 200);
	memset(data[0]+100, 240, 100);
	
	int val = 130;
	int bars=25;
	int pi;

	int ni;
	vevo_get_property( plug->self, VEVOI_N_IN_CHANNELS, &ni );

	for(i = 0; i < ni; i ++ )
	{
		uint8_t* frame[3];
		int		value = 1;
		int 		len   = 10 * 10;
		vevo_pixel_info_t type = VEVO_FRAME_U8;

		vevo_set_property( plug->in_channels[i], VEVOC_WIDTH,
			VEVO_INT, 1, &width );
		vevo_set_property( plug->in_channels[i], VEVOC_HEIGHT,	
			VEVO_INT, 1, &height );
		vevo_set_property( plug->in_channels[i], VEVOC_PIXELINFO,
			VEVO_INT, 1, &type );
		vevo_set_property( plug->in_channels[i], VEVOC_SHIFT_V,
			VEVO_INT, 1, &value );	
		vevo_set_property( plug->in_channels[i], VEVOC_SHIFT_H,
			VEVO_INT, 1, &value );

		frame[0] = data[0]+(i*len);
		frame[1] = data[1]+(i*len);
		frame[2] = data[2]+(i+len);

		vevo_set_property( plug->in_channels[i], VEVOC_PIXELDATA, 
			VEVO_PTR_U8, 3, frame );
	
	}

	return plug;
}

int main(int argc, char *argv[])
{
	void *handle;
	int err;  
	int i;

	handle = dlopen( argv[1], RTLD_LAZY);
	if(!handle)
	{
		printf("Can't open plugin %s\n", argv[1]);
		return 0;
	}
	vevo_setup_f *vevo_setup = (vevo_setup_f*) dlsym(handle, "vevo_setup");
	if(!vevo_setup) 
	{
		printf("Can't find mandatory function 'vevo_setup' in plugin %s\n",
			argv[1]);
		return 0;
	}

	vevo_instance_templ_t *tmpl = (*vevo_setup)();
	if(!tmpl) 
	{		
		printf("Can't get template from plugin %s\n", argv[1]);
		return 0;
	}
	vevo_init_f	*init = tmpl->init;
	if(!init)
	{
		printf("Can't find mandatory function 'init' in plugin %s\n",
			argv[1]);		
		return 0;
	}	

	// ready data
	vevo_instance_t *inst = readyfy_plugin( tmpl );

	dump_host_info();
	dump_plugin_info(inst, tmpl);


	// ready init
	if ( init(inst) == 0 )
		out(1,"Called init() succesfully\n");

	// initialize parameters (either own or DEFAULTS)

	test_parameters( inst );

	// run process
	vevo_process_f	*process = tmpl->process;
	if(!process)
	{
		printf("Can't find mandatory function 'process' in plugin %s\n",
			argv[1]);
		return 0;
	}

	err = process(inst);
	if( err == 0 )
		out(1, "Called process() successfully\n");
	else
		out(1, "Error in process(): %d\n", err);

	vevo_deinit_f	*deinit = tmpl->deinit;
	if(!deinit)
	{
		printf("Can't find mandatory function 'deinit' in plugin %s\n",
			argv[1]);
		return 0;
	}

	if ( deinit(inst) != 0 )
		out(1,"Error calling deinit()\n");

	vevo_free_instance( inst );

	dlclose( handle );
	return 1;
}
