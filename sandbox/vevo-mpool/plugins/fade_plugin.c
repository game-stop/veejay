#include <stddef.h>
#include <stdio.h>
#include <include/livido.h>

LIVIDO_PLUGIN

#include "livido-utils.c"


#define num_palettes 5

int palettes[num_palettes] = {	LIVIDO_PALETTE_RGB565, 
				LIVIDO_PALETTE_RGB888, 
				LIVIDO_PALETTE_RGBA8888,
				LIVIDO_PALETTE_YUV888, 
				LIVIDO_PALETTE_YUVA8888 
			} ;


livido_init_f init_instance (livido_port_t *my_instance) 
{
	livido_set_string_value(my_instance, "PLUGIN_mydata", "My personal data, that the host won't touch!!!");
	return 0;
}

livido_init_f deinit_instance (livido_port_t* my_instance) 
{
	// we would do cleanup and freeing here, but we have nothing to do
	return 0;
}

livido_process_f process_frame(	livido_port_t *my_instance,
				double timecode)
{
	return 0;
}


#ifdef FUNCSTRUCT
livido_port_t *livido_setup(livido_setup_t *list, int vversion)
#else
livido_port_t *livido_setup(livido_setup_t list[], int vversion)
#endif
{
	livido_port_t *info;
	livido_port_t *filter1;
	livido_port_t *in_chann, *out_chann, *in_param, *out_param;

	LIVIDO_IMPORT( list );


	info = livido_port_new(LIVIDO_PORT_TYPE_PLUGIN_INFO);
	livido_set_string_value(info, "maintainer", "Andraz Tori");
	livido_set_string_value(info, "version", "1.0");
	
	filter1 = livido_port_new(LIVIDO_PORT_TYPE_FILTER_CLASS);
		livido_set_string_value	(filter1, "name", "Fade plugin");
		livido_set_string_value	(filter1, "description", "Fades the image with alpha channel when exists or to black when it does not");
		livido_set_int_value	(filter1, "version", 1);
		livido_set_int_value	(filter1, "api_version", 100);
		livido_set_string_value	(filter1, "license", "Public domain");
		livido_set_int_value	(filter1, "flags", LIVIDO_FILTER_CAN_DO_INPLACE | LIVIDO_FILTER_STATELESS);
		livido_set_voidptr_value(filter1, "process_func", &process_frame);
		livido_set_voidptr_value(filter1, "init_func", &init_instance);
		livido_set_voidptr_value(filter1, "deinit_func", &deinit_instance);
		
		in_chann = livido_port_new(LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE);
			livido_set_string_value	(in_chann, "name", "input");
			livido_set_int_value	(in_chann, "flags", 0);
			livido_set_int_array	(in_chann, "palette_list", num_palettes, palettes);
		livido_set_portptr_value(filter1, "in_channel_templates", in_chann);
		
		out_chann = livido_port_new(LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE);
			livido_set_string_value	(out_chann, "name", "faded output");
			livido_set_int_value	(out_chann, "flags", 0);
			livido_set_int_array	(out_chann, "palette_list", num_palettes, palettes);
			livido_set_voidptr_value (out_chann, "same_as_size", in_chann);
			livido_set_voidptr_value (out_chann, "same_as_palette", in_chann);
		livido_set_portptr_value(filter1, "out_channel_templates", in_chann);
		
		in_param = livido_port_new(LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE);
			livido_set_string_value	(in_param, "name", "Fade percentage");
			livido_set_int_value	(in_param, "flags", 0);
			livido_set_string_value (in_param, "kind", "NUMBER");
			livido_set_string_value (in_param, "PLUGIN_kinda", "NUMBER");
			livido_set_double_value	(in_param, "default", 0.5);
			livido_set_double_value	(in_param, "min", 0.0);
			livido_set_double_value	(in_param, "max", 0.0);
			livido_set_boolean_value (in_param, "transition", 1);			
			
		livido_set_portptr_value(filter1, "in_parameter_templates", in_param);
		
		// Create an empty array of output templates
		livido_set_portptr_array(filter1, "out_parameter_templates", 0, 0);
		

		// Just to demonstrate - conformancy checking host will not complain about our own PLUGIN_ variable
		livido_set_string_value (filter1, "PLUGIN_my_stuff", "I can put here whatever i want");

	livido_set_portptr_value(info, "filters", filter1);
	
	return info;
}



