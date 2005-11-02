#include <include/vevo.h>
#include <include/livido.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

static struct {
	int32_t iv;
	double dv;
	char *sv;
	int32_t *bv;
} fundementals[] =
{
	{ 10, 123.123, "a medium length string to store", FALSE },
	{ 20, 1234567, "some other string", TRUE },
	{ 1234, 0.123456, "and another", FALSE }, 	
	{ 65535, 0.999123, "this is getting very boring", TRUE },
	{0,0.0,NULL, FALSE}
};

static	struct	{
	int32_t	iv[4];
	double	dv[4];
	char    *sv[4];
	int32_t *bv[4];
} arrays[] =
{
	{ {1,2,3,4}, {1.0,2.0,3.0,4.0}, { "abc", "meet", "work", "beer" }, { FALSE,TRUE,TRUE,TRUE } },
	{ {5,6,7,8}, {1.1,2.1,3.1,4.1}, { "livido", "src", "gpl", "segfault"},{ FALSE,TRUE,FALSE,TRUE } },
	{ {9,10,11,12},{ 11.11,12.12,13.13,14.14}, { "freeze", "unfreeze", "overlay", "multiply" },{FALSE,FALSE,FALSE,FALSE} },
};

static int array_index = 0;
static int fundemental_index = 0;

int	test_fundemental_atoms(livido_port_t *port)
{
	livido_property_set( port, "int_value", LIVIDO_ATOM_TYPE_INT, 1,&(fundementals[fundemental_index].iv) );
	livido_property_set( port, "double_value", LIVIDO_ATOM_TYPE_DOUBLE, 1,&(fundementals[fundemental_index].dv) );
	livido_property_set( port, "string_value", LIVIDO_ATOM_TYPE_STRING, 1,&(fundementals[fundemental_index].sv));
	livido_property_set( port, "bool_value", LIVIDO_ATOM_TYPE_BOOLEAN, 1,&(fundementals[fundemental_index].bv));
	fundemental_index ++;

	return 0;
}

void	test_arrays(livido_port_t *port)
{
	livido_property_set( port, "int_values", LIVIDO_ATOM_TYPE_INT, 4,arrays[array_index].iv );
	livido_property_set( port, "double_values", LIVIDO_ATOM_TYPE_DOUBLE, 4,arrays[array_index].dv );
	livido_property_set( port, "string_values", LIVIDO_ATOM_TYPE_STRING, 4,arrays[array_index].sv);
	livido_property_set( port, "bool_values", LIVIDO_ATOM_TYPE_BOOLEAN, 4,arrays[array_index].bv);
	array_index ++;
}

void	test_store_empty_atoms( livido_port_t *port )
{
	char *ref = "spooky data";
	livido_property_set( port, "ghost", LIVIDO_ATOM_TYPE_STRING, 0, NULL );
	livido_property_set( port, "spooky", LIVIDO_ATOM_TYPE_STRING, 1, &ref );
}

void	dump_empty_atoms(livido_port_t *port)
{
	char *ghost = NULL;
	if(livido_property_get(port, "ghost", 0, NULL ) == LIVIDO_NO_ERROR)
	{
		printf("\tEmpty property 'ghost' exists\n"  );
	}

	if(livido_property_get(port, "spooky", 0, NULL ) == LIVIDO_NO_ERROR)
	{
		printf("\tProperty 'spooky' Exists\n"  );
	}
	int size = livido_property_element_size( port, "spooky",0);
	char *spook = (char*) malloc(sizeof(char) * size);
	memset(spook, 0, size);
	if( livido_property_get(port, "spooky", 0, spook ) == LIVIDO_NO_ERROR)
		printf("\tProperty 'spooky' has value '%s'\n", spook );
	if(spook) free(spook);
}
void break_here()
{
}
void	dump_arrays(livido_port_t *port )
{
	int32_t	int_value[4];
	double  double_value[4];
	char    *string_value[4];
	int32_t bool_value[4];
	livido_property_get( port, "int_values",-1, int_value );
	livido_property_get( port, "double_values",-1, double_value );

	/* allocate space for string list */
	int j;
	for(j = 0; j < 4; j ++ )
	{
		int ssize = livido_property_element_size(port, "string_values", j );
		string_value[j] = (char*)malloc(sizeof(char)*ssize);
		memset( string_value[j], 0, ssize );
	}

	livido_property_get( port, "string_values",-1,string_value );
	livido_property_get( port, "bool_values", -1,bool_value );

	int i;
	for( i = 0; i < 4; i ++ )
	{
		printf("\tElement %d of int_values has value %d\n", i, int_value[i]);
		printf("\tElement %d of double_values has value %g\n", i, double_value[i]);
		printf("\tElement %d of string_values has value '%s'\n", i, string_value[i]);
		printf("\tElement %d of bool_values has value %d\n", i, bool_value[i]);
		free(string_value[i]);
	}
}
void	dump_port(livido_port_t *port )
{
	int32_t	int_value = 0;
	double  double_value = 0.0;
	char    string_value[100];
	int32_t bool_value = FALSE;
	bzero(string_value,100);
	livido_property_get( port, "int_value",0, &int_value );
	livido_property_get( port, "double_value",0, &double_value );
	livido_property_get( port, "string_value",0, &string_value );
	livido_property_get( port, "bool_value", 0,&bool_value );

	printf("\tProperty int_value has value %d\n", int_value );
	printf("\tProperty double_value has value %g\n", double_value);
	printf("\tProperty string value has value '%s'\n", string_value );
	printf("\tProperty bool value has value %d\n", bool_value );

}
void	dump_ptr_port(livido_port_t *port )
{
	uint8_t *pixel_data[4];
	livido_property_get( port, "pixeldata", -1,&pixel_data );
	livido_port_t *ports[4];

	printf("\tProperty pixeldata: {%p, %p, %p, %p}\n", 
		pixel_data[0],
		pixel_data[1],
		pixel_data[2],
		pixel_data[3]);

	int i;
	for( i = 0; i < 4; i ++ )
	{
		uint8_t *plane;
		livido_property_get( port, "pixeldata", i, &plane );
		printf("\tProperty pixeldata by index %d = %p\n",i, plane );
	}

	livido_property_get( port, "ports",-1, &ports );

	printf("\tProperty ports:  (%p, %p, %p, %p}\n",
		ports[0],ports[1],ports[2],ports[3]);
	for( i = 0; i < 4; i ++ )
	{
		livido_port_t *p;
		livido_property_get( port, "ports", i, &p );
		printf("\tProperty ports by index %d = %p\n",i, p );
	}

}

int main(int argc, char *argv[])
{

	livido_port_t *port = livido_port_new( 0 );
	printf("Testing fundementals\n");
	while( fundementals[fundemental_index].sv != NULL )
	{
		test_fundemental_atoms(port);
		dump_port( port );
	}

	printf("Test voidptr and portptr atom types\n");
	uint8_t *pixel_data[4];
	int i;
	for( i = 0; i < 4; i ++ )
		pixel_data[i] = (uint8_t*) malloc(sizeof(uint8_t) * 100);
	printf("\tpixel_data %p, %p, %p, %p\n",
		pixel_data[0],pixel_data[1],pixel_data[2],pixel_data[3]);

	livido_port_t *ports[4] = { port,port, port, port };
	printf("\tport__data %p, %p, %p, %p\n", port,port,port,port );	
	livido_property_set( port, "pixeldata", LIVIDO_ATOM_TYPE_VOIDPTR, 4,pixel_data);
	livido_property_set( port, "ports", LIVIDO_ATOM_TYPE_PORTPTR, 4,ports );
	dump_ptr_port( port );	


	printf("Testing storing empty atoms of any type\n");

	test_store_empty_atoms( port );
	dump_empty_atoms(port);

	for( i = 0; i < 3; i ++ )
	{
		printf("Test arrays of fundementals\n");
		test_arrays(port);
		dump_arrays(port);  
	}

	printf("Freeing port %p\n", port );
	printf("Dumping properties in port %p\n",port);

	char **list = livido_list_properties( port );

	livido_port_free(port);	

	for( i = 0; list[i] != NULL ; i ++ )
	{
		printf("\tproperty %s\n", list[i]);
		free(list[i]);
	}
	if(list ) free(list);

	for( i = 0; i < 4; i ++ )
		free(pixel_data[i] );

	return 1;

}
