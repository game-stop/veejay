#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <include/vevo.h>
#include <include/livido.h>
static struct {
	int32_t iv;
	double dv;
	char *sv;
	int32_t *bv;
} fundementals[] =
{
	{ 10, 123.123, "parameter value 1", FALSE },
	{ 20, 1234567, "parameter value 2", TRUE },
	{ 1234, 0.123456, "parameter value 3", FALSE }, 	
	{ 65535, 0.999123, "parameter value 4", TRUE },
	{0,0.0,NULL, FALSE}
};

static	struct	{
	int32_t	iv[4];
	double	dv[4];
	char    *sv[4];
	int32_t *bv[4];
} arrays[] =
{
	{ {1,2,3,4}, {1.0,2.0,3.0,4.0}, { "green", "red", "blue", "alpha" }, { FALSE,TRUE,TRUE,TRUE } },
	{ {5,6,7,8}, {1.1,2.1,3.1,4.1}, { "x", "y", "z", "t"},{ FALSE,TRUE,FALSE,TRUE } },
	{ {9,10,11,12},{ 11.11,12.12,13.13,14.14}, { "freeze", "unfreeze", "overlay", "multiply" },{FALSE,FALSE,FALSE,FALSE} },
};

static int array_index = 0;
static int fundemental_index = 0;

static int	stats[2] = { 0,0};

int	test_fundemental_atoms(livido_port_t *port)
{
	livido_property_set( port, "int_value", LIVIDO_ATOM_TYPE_INT, 1,&(fundementals[fundemental_index].iv) );
	livido_property_set( port, "double_value", LIVIDO_ATOM_TYPE_DOUBLE, 1,&(fundementals[fundemental_index].dv) );
	livido_property_set( port, "string_value", LIVIDO_ATOM_TYPE_STRING, 1,&(fundementals[fundemental_index].sv));
	livido_property_set( port, "bool_value", LIVIDO_ATOM_TYPE_BOOLEAN, 1,&(fundementals[fundemental_index].bv));
	fundemental_index ++;
	stats[0] += 4;
	return 0;
}

void	test_arrays(livido_port_t *port)
{
	livido_property_set( port, "int_values", LIVIDO_ATOM_TYPE_INT, 4,arrays[array_index].iv );
	livido_property_set( port, "double_values", LIVIDO_ATOM_TYPE_DOUBLE, 4,arrays[array_index].dv );
	livido_property_set( port, "string_values", LIVIDO_ATOM_TYPE_STRING, 4,arrays[array_index].sv);
	livido_property_set( port, "bool_values", LIVIDO_ATOM_TYPE_BOOLEAN, 4,arrays[array_index].bv);
	stats[0] += 4;
	array_index ++;
}
#include <sys/time.h>
void	more_properties(livido_port_t *port )
{
	struct timeval t;	
	memset(&t, 0, sizeof(struct timeval));
	char key[40];
	sprintf(key, "%ld%ld",t.tv_usec,t.tv_sec );
	int test = (int) ( 100.0 * rand() / (RAND_MAX+1.0));
	livido_property_set( port, key, LIVIDO_ATOM_TYPE_INT, 1, &test );
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

	stats[1] += 4;

	int i;
	for( i = 0; i < 4; i ++ )
	{
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

	stats[1] += 4;
}

int main(int argc, char *argv[])
{

	livido_port_t *port = livido_port_new( 0 );

	int	max = 100;
	int 	i = 0;

	if( argc == 2 )
		max = atoi( argv[1] );

	for( i = 0; i < max; i ++ ) // get and set 16 properties per cycle
	{
		/* test 4 parameters, with only fundementals. 1 put and 1 get per property */
		while( fundementals[fundemental_index].sv != NULL )
		{
			test_fundemental_atoms(port);
			dump_port( port );
		}

		/* test 4 parameters, array of atoms */
		int j;
		for( j = 0; j < 3; j ++ )
		{
			test_arrays(port);
			dump_arrays(port);  
		}

		fundemental_index = 0;
		array_index = 0;

		//more_properties(port); // grow port with random empty properties

	}


	livido_port_free(port);	

	printf("%d livido_property_put, %d livido_property_get \n", stats[0],stats[1]);

	return 1;

}
