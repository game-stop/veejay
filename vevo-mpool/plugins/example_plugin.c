#include <stddef.h>
#include "stdio.h"
#include <include/livido.h>

LIVIDO_PLUGIN

livido_init_f init_instance (livido_port_t *my_instance) 
{
	return 0;
}

livido_init_f deinit_instance (livido_port_t* my_instance) 
{
	return 0;
}

livido_process_f process_frame(	livido_port_t *my_instance,
				double timecode)
{
	return 0;
}

livido_port_t *livido_setup(livido_setup_t *list,int vversion)
{
	livido_port_t *info = NULL;

	char *name = "Niels Elburg";
	int  version = 101;
	int  i = 55;

	LIVIDO_IMPORT( list );

	info = livido_port_new( i );

	livido_property_set( info, "name", LIVIDO_ATOM_TYPE_STRING ,1, &name );
	livido_property_set( info, "PLUGIN_foo", LIVIDO_ATOM_TYPE_INT,1,  &i);
	livido_property_set( info, "PLUGIN_bar", LIVIDO_ATOM_TYPE_STRING,1, &name);
	
	return info;
}


