
#include <dlfcn.h>
#include <stdio.h>       
#include <string.h>
#include <stdlib.h>

#include "../include/vevo.h"
#include "../include/livido.h"
#include "../src/livido-utils.c"

int verbose = 1;

int errorcount = 0;
int warningcount = 0;

typedef int (deepcheck_f)(livido_port_t *, char *);
typedef struct { 
		int ver;	// -1 - end sentinel, 1 for specification 100
		int sets;	// 1 - host, 2 - plugin, 3 - both
		int mand;	// 0 - optional, 1 - mandatory
		char *name;	// name of the property
		int atomtype;	// atom type of the property, -1 - don't check - should be checked by func()
		int minc;	// minimum number of elements, -1 - no limit
		int maxc;	// maximum number of elements, -1 - no limit
		deepcheck_f *func;	// deep check function to be called
		} property_desc_t;

deepcheck_f deep_plugininfo_filters;
deepcheck_f deep_channel_templates;
deepcheck_f deep_parameter_templates;
deepcheck_f deep_filter_api_version;
int test_port (livido_port_t *port, property_desc_t *properties, int porttype, char *portdesc);


#define DESC_END {-1, 0, 0, "",		0,				0,  0  , NULL}


property_desc_t port_plugininfo_desc[] = {
	{1, 2, 1, "type", 		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 1, "filters",		LIVIDO_ATOM_TYPE_PORTPTR,	1,  -1 , &deep_plugininfo_filters},
	{1, 2, 1, "maintainer",		LIVIDO_ATOM_TYPE_STRING,	1,  1  , NULL},
	{1, 2, 1, "version",		LIVIDO_ATOM_TYPE_STRING,	1,  1  , NULL},
	{1, 2, 0, "url",		LIVIDO_ATOM_TYPE_STRING,	1,  1  , NULL},
	DESC_END
}; 

property_desc_t port_filterclass_desc[] = {
	{1, 2, 1, "type", 		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 1, "name", 		LIVIDO_ATOM_TYPE_STRING, 	1,  1  , NULL},
	{1, 2, 1, "description", 	LIVIDO_ATOM_TYPE_STRING, 	1,  1  , NULL},
	{1, 2, 1, "version", 		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 1, "api_version",	LIVIDO_ATOM_TYPE_INT, 		1,  1  , &deep_filter_api_version},
	{1, 2, 1, "license",	 	LIVIDO_ATOM_TYPE_STRING, 	1,  1  , NULL},
	{1, 2, 1, "flags",		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 1, "init_func",		LIVIDO_ATOM_TYPE_VOIDPTR,	1,  1  , NULL},
	{1, 2, 1, "process_func",	LIVIDO_ATOM_TYPE_VOIDPTR,	1,  1  , NULL},
	{1, 2, 1, "deinit_func",	LIVIDO_ATOM_TYPE_VOIDPTR,	1,  1  , NULL},
	{1, 2, 1, "in_channel_templates",	LIVIDO_ATOM_TYPE_PORTPTR,	0,  -1 , &deep_channel_templates},
	{1, 2, 1, "out_channel_templates",	LIVIDO_ATOM_TYPE_PORTPTR,	0,  -1 , &deep_channel_templates},
	{1, 2, 1, "in_parameter_templates",	LIVIDO_ATOM_TYPE_PORTPTR,	0,  -1 , &deep_parameter_templates},
	{1, 2, 1, "out_parameter_templates",	LIVIDO_ATOM_TYPE_PORTPTR,	0,  -1 , &deep_parameter_templates},
	{1, 2, 0, "url", 		LIVIDO_ATOM_TYPE_STRING, 	1,  1  , NULL},
	DESC_END
}; 

property_desc_t port_chantemplate_desc[] = {
	{1, 2, 1, "type", 		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 1, "name", 		LIVIDO_ATOM_TYPE_STRING, 	1,  1  , NULL},
	{1, 2, 1, "flags",		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 1, "palette_list",	LIVIDO_ATOM_TYPE_INT, 		1,  -1 , NULL},
	{1, 2, 0, "description",	LIVIDO_ATOM_TYPE_STRING, 	1,  1  , NULL},
	{1, 2, 0, "width",		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 0, "height",		LIVIDO_ATOM_TYPE_INT,	 	1,  1  , NULL},
	{1, 2, 0, "same_as_size",	LIVIDO_ATOM_TYPE_PORTPTR,	1,  1  , NULL},
	{1, 2, 0, "same_as_palette",	LIVIDO_ATOM_TYPE_PORTPTR,	1,  1  , NULL},
	{1, 2, 0, "optional",		LIVIDO_ATOM_TYPE_BOOLEAN,	1,  1  , NULL},
	DESC_END
}; 

property_desc_t port_paramtemplate_desc[] = {
	{1, 2, 1, "type", 		LIVIDO_ATOM_TYPE_INT, 		1,  1  , NULL},
	{1, 2, 1, "name", 		LIVIDO_ATOM_TYPE_STRING, 	1,  1  , NULL},
	{1, 2, 1, "kind",		LIVIDO_ATOM_TYPE_STRING, 	1,  1 , NULL},
	{1, 2, 1, "flags",		LIVIDO_ATOM_TYPE_INT,	 	1,  1 , NULL},
	{1, 2, 0, "description",	LIVIDO_ATOM_TYPE_STRING, 	1,  1 , NULL},
	DESC_END
}; 

property_desc_t port_paramtemplate_number_desc[] = {
	{1, 2, 1, "default",		LIVIDO_ATOM_TYPE_DOUBLE,	1,  1  , NULL},
	{1, 2, 1, "min", 		LIVIDO_ATOM_TYPE_DOUBLE, 	1,  1  , NULL},
	{1, 2, 1, "max",		LIVIDO_ATOM_TYPE_DOUBLE,	1,  1  , NULL},
	{1, 2, 0, "wrap",		LIVIDO_ATOM_TYPE_BOOLEAN, 	1,  1 , NULL},
	{1, 2, 0, "transition",		LIVIDO_ATOM_TYPE_BOOLEAN, 	1,  1 , NULL},
	DESC_END
}; 

property_desc_t port_paramtemplate_index_desc[] = {
	{1, 2, 1, "default",		LIVIDO_ATOM_TYPE_INT,	1,  1  , NULL},
	{1, 2, 1, "min", 		LIVIDO_ATOM_TYPE_INT, 	1,  1  , NULL},
	{1, 2, 1, "max",		LIVIDO_ATOM_TYPE_INT,	1,  1  , NULL},
	{1, 2, 0, "wrap",		LIVIDO_ATOM_TYPE_BOOLEAN, 	1,  1 , NULL},
	DESC_END
}; 
property_desc_t port_paramtemplate_text_desc[] = {
	{1, 2, 1, "default",		LIVIDO_ATOM_TYPE_STRING,1,  1  , NULL},
	DESC_END
}; 

property_desc_t port_paramtemplate_switch_desc[] = {
	{1, 2, 1, "default",		LIVIDO_ATOM_TYPE_BOOLEAN,1,  1  , NULL},
	DESC_END
}; 

property_desc_t port_paramtemplate_colorrgba_desc[] = {
	{1, 2, 1, "default",		LIVIDO_ATOM_TYPE_DOUBLE,3,  4  , NULL},
	{1, 2, 1, "min",		LIVIDO_ATOM_TYPE_DOUBLE,3,  4  , NULL},
	{1, 2, 1, "max",		LIVIDO_ATOM_TYPE_DOUBLE,3,  4  , NULL},
	DESC_END
}; 

property_desc_t port_paramtemplate_coordinate_desc[] = {
	{1, 2, 1, "default",		LIVIDO_ATOM_TYPE_DOUBLE,2,  2  , NULL},
	{1, 2, 1, "min",		LIVIDO_ATOM_TYPE_DOUBLE,2,  2  , NULL},
	{1, 2, 1, "max",		LIVIDO_ATOM_TYPE_DOUBLE,2,  2  , NULL},
	DESC_END
}; 

typedef struct {
		char *name; 
		property_desc_t *property_desc;
	} kind_parameter_desc_t;
	
kind_parameter_desc_t kind_descs[] = {
		{"INDEX",	port_paramtemplate_index_desc},
		{"NUMBER",	port_paramtemplate_number_desc},
		{"TEXT",	port_paramtemplate_text_desc},
		{"SWITCH",	port_paramtemplate_switch_desc},
		{"COLOR_RGBA",	port_paramtemplate_colorrgba_desc},
		{"COORDINATE",	port_paramtemplate_coordinate_desc},
		{NULL, NULL}
	};


property_desc_t *create_joint(property_desc_t *p1, property_desc_t *p2)
{
	int p1num, p2num;
	property_desc_t *joint;
	
	for (p1num = 0; p1[p1num].ver != -1; p1num++);
	for (p2num = 0; p2[p2num].ver != -1; p2num++);
	
	joint = malloc(sizeof(property_desc_t) * (p1num + p2num + 1));
	memcpy (joint, p1, sizeof(property_desc_t) * p1num);
	memcpy (joint + p1num, p2, sizeof(property_desc_t) * (p2num + 1));

	return joint;
}


int deep_filter_api_version(livido_port_t *port, char *propname)
{
	int error;
	int api_version = livido_get_int_value(port, propname, &error);
	if (api_version != 100)
	{
		printf("WARNING: Filter's api_version is %i, this host has 100\n", api_version	);
		warningcount++;
	}
	return 0;
}

int deep_channel_templates(livido_port_t *port, char *propname)
{
	int numtemplates = livido_property_num_elements(port, propname);
	int error;
	livido_port_t **ports = (livido_port_t **) livido_get_portptr_array (port, propname, &error);
	for (int i = 0; i < numtemplates; i++)
	{
		char tmp[1024];
		sprintf(tmp, "%s number %i inside PLUGIN_INFO", propname, i);
		test_port(ports[i], port_chantemplate_desc, LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE, tmp);
	}
	return 0;
}

int deep_parameter_templates(livido_port_t *port, char *propname)
{
	int numtemplates = livido_property_num_elements(port, propname);
	int error;
	livido_port_t **ports = (livido_port_t **) livido_get_portptr_array (port, propname, &error);
	for (int i = 0; i < numtemplates; i++)
	{
		char tmp[1024];
		sprintf(tmp, "%s number %i inside PLUGIN_INFO", propname, i);
		property_desc_t *curport;
		char *kindstr = livido_get_string_value(ports[i], "kind", &error);
		if (error || kindstr == NULL)
		{
			printf("ERROR: Parameter template port (%i) does not have '''kind''' property", i);
			errorcount++;
			continue;
		}
		
		// Find the kind desc and join it with general parameter template desc
		kind_parameter_desc_t *kinds;		
		for (kinds = kind_descs; (kinds->name != NULL) && strcmp(kindstr, kinds->name); kinds++);
	
		if (kinds->name == NULL)
		{
			printf("ERROR: Uknown '''kind''' %s of parameter template port %i\n", kindstr, i);
			errorcount++;
			continue; // since we didn't do the join skip the check;
		
		} else
		{		
			curport = create_joint(port_paramtemplate_desc, kinds->property_desc);
		};
		
		test_port(ports[i], curport, LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE, tmp);
		free(curport);
	}
	return 0;
}



int deep_plugininfo_filters(livido_port_t *port, char *propname)
{
	int numfilters = livido_property_num_elements(port, propname);
	int error;
	livido_port_t **ports = (livido_port_t **) livido_get_portptr_array (port, propname, &error);
	for (int i = 0; i < numfilters; i++)
	{
		char tmp[1024];
		sprintf(tmp, "FILTER_CLASS number %i inside PLUGIN_INFO", i);
		test_port(ports[i], port_filterclass_desc, LIVIDO_PORT_TYPE_FILTER_CLASS, tmp);
	}

	return 0;
}

// Implementation of default malloc/free/memset/memcpy
void *livido_malloc_f (size_t size) 				{ return malloc(size); }
void livido_free_f (void *ptr)					{ free(ptr); }
void *livido_memset_f (void *s, int c, size_t n)		{ return memset(s, c, n); }
void *livido_memcpy_f (void *dest, const void *src, size_t n)	{ return memcpy( dest, src, n); }




int check_property_mandatory (livido_port_t *port, char *propname, char *propdesc)
{
	int error =  livido_property_get(port, propname, 0, NULL);
	if (error) 
	{
		printf("ERROR: Missing mandatory property \'\'%s\'\' of port %s, code: %i\n", propname, propdesc, error);
		errorcount ++;		
		return -1;
	}	
	return 0;
}
int check_property_type (livido_port_t *port, char *propname, int atom_type, char *propdesc)
{
	int error =  livido_property_get(port, propname, 0, NULL);
	if (!error) 
	{
		int real_atomtype =  livido_property_atom_type(port, propname);
		if (real_atomtype != atom_type) 
		{
			printf("ERROR: Type of property %s is %i, must be: %i\n", propdesc, real_atomtype, atom_type);
			errorcount ++;
			return 0;
		}
	}	
	return 0; // returning ok when no type defined
}
int check_property_type_mandatory (livido_port_t *port, char *propname, int atom_type, char *propdesc)
{
	int retval = check_property_mandatory(port, propname, propdesc);
	if (retval) 
		return retval;
	return check_property_type(port, propname, atom_type, propdesc);
}	

int check_property_type_mandatory_one (livido_port_t *port, char *propname, int atom_type, char *propdesc)
{
	int retval = check_property_type_mandatory(port, propname, atom_type, propdesc);
	if (retval) 
		return retval;
	if (livido_property_num_elements(port, propname) != 1)
		return -1;
	return 0;
}	

// checks if "type" property of the port exists and is of correct atom type, and then check if the value matches porttype
void check_port_type (livido_port_t *port, int porttype, char *portdesc)
{
	char tmp[1024];
	sprintf(tmp, "type property of port %s\n", portdesc);
	int retval = check_property_type_mandatory(port, "type", LIVIDO_ATOM_TYPE_INT, tmp);
	if (retval)
	{
		printf("FATAL: Last error was fatal\n");
		exit(1);
	} else
	{
		int error;
		int real_type = livido_get_int_value(port, "type", &error);
		// error cannot happen at this point, since we already checked mandatority and atom type
		if (real_type != porttype)
		{
			printf("FATAL: Type of port %s must be %i, but is %i\n", portdesc, porttype, real_type);
			exit(1);
		}
	}	
}

int test_port (livido_port_t *port, property_desc_t *properties, int porttype, char *portdesc)
{
	// First test the port type
	check_port_type(port, porttype, portdesc);
	
	// Now check for properties not in standard
	char **property_names = livido_list_properties(port);    // FIXME: Who frees this memory?
	for (char **lookup_key = property_names; *lookup_key != 0; lookup_key++ ) 		
	{
		int property_in_standard = 0;
		for (property_desc_t *standard_property = properties; standard_property->ver != -1; standard_property++)
		{
			if (!strcmp(standard_property->name, *lookup_key))
				property_in_standard = 1;
		}
		if (!strncmp(*lookup_key, "PLUGIN_", 7))
			property_in_standard = 1;

		if (!strncmp(*lookup_key, "HOST_", 5))		
			property_in_standard = 1;
			
		if (!property_in_standard)
		{
			printf("WARNING: Property \'%s\' of port %s is not defined in standard\n", *lookup_key, portdesc);
			warningcount++;
		}
	}
	
	// now go trough a list of port's properties defined by standard and check them
	for (property_desc_t *standard_property = properties; standard_property->ver != -1; standard_property++)
	{
		char *propname = standard_property->name;
		// check existence
		int error =  livido_property_get(port, propname, 0, NULL);
		if (error == LIVIDO_ERROR_NOSUCH_PROPERTY) // we ignore no such element property here, we deal with that later!
		{
			// property does not exist
			if (standard_property->mand)
			{
				printf("ERROR: Missing mandatory property \'\'%s\'\' of port %s\n", propname, portdesc);
				errorcount ++;		
			}
			continue;
		}	
		printf("Property %s, %d\n", propname, error);
		// Property exists, check type
		int atomtype =  livido_property_atom_type(port, propname);
		if (standard_property->atomtype >= 0 && standard_property->atomtype != atomtype) 
		{
			printf("ERROR: Type of property \'\'%s\'\' (of port %s) is %i, must be: %i\n", propname, portdesc, atomtype, standard_property->atomtype);
			errorcount ++;
			continue;
		}
		// Property exists and has right type, check if array is properly laid out
		int numelements = livido_property_num_elements(port, propname);
		if (standard_property->minc >= 0 && standard_property->minc > numelements)
		{
			printf("ERROR: Property \'\'%s\'\' (of port %s) has %i elements, which is less than minimum of %i\n", propname, portdesc, numelements, standard_property->minc);
			errorcount ++;
			continue;
		}
		if (standard_property->maxc >= 0 && standard_property->maxc < numelements)
		{
			printf("ERROR: Property \'\'%s\'\' (of port %s) has %i elements, which is more than maximum of %i\n", propname, portdesc, numelements, standard_property->minc);
			errorcount ++;
			continue;
		}
		// FIXME: We might want to call deep check functions when we already checked _all_ properties
		if (standard_property->func)
		{
			standard_property->func(port, propname);
		}
	}

	return 0;	
}

int main(int argc, char **argv) 
{

	char *name;
	void *handle;
	livido_setup_f livido_setup;
	livido_port_t *plugin_info;


	if (argc != 2) {
		printf("Livido conformance testing host 0.1\n");
		printf("Usage: testhost plugin_name.so\n");
		return 0;
	}
	name = argv[1];
	
	handle = dlopen(name, RTLD_NOW);     // We want the whole load _now_
	if (!handle) { printf("FATAL: dlopen failed on %s because of %s\n", name, dlerror()); return 1; };
	
	livido_setup = (livido_setup_f) dlsym(handle, "livido_setup");
	if (!livido_setup) { printf("FATAL: function livido_setup not found in %s\n", name); return 1; };

	plugin_info = livido_setup();
	if (!plugin_info) { printf("FATAL: livido_setup() did not return a pointer to livido port, finishing\n"); return 1; };
 	printf("CHECKPOINT: Loading of plugin and running livido_setup() successeful\n");
 	
 	
 	test_port(plugin_info, port_plugininfo_desc, LIVIDO_PORT_TYPE_PLUGIN_INFO, "returned by livido_setup()");


	dlclose(handle);
	
	printf("\nPlugin %s has produced %i errors and %i warnings\n", name, errorcount, warningcount);

// FIXME: Do the freeing of the all template ports
	
	return 0;
}
