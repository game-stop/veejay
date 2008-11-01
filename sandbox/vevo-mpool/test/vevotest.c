#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include <include/libvevo.h>

static struct {
    int32_t iv;
    double dv;
    char *sv;
    int32_t bv;
} fundementals[] = {
    {
    10, 123.123, "a medium length string to store", FALSE}, {
    20, 999.567, "some other string", TRUE}, {
    1234, 0.123456, "and another", FALSE}, {
    65535, 0.999123, "this is getting very boring", TRUE}, {
    0, 0.0, NULL, FALSE}
};

static struct {
    int32_t iv[4];
    double dv[4];
    char *sv[4];
    int32_t bv[4];
} arrays[] = {
    { {
    1, 2, 3, 4}, {
    1.0, 2.0, 3.0, 4.0}, {
    "abc", "meet", "work", "beer"}, {
    FALSE, TRUE, TRUE, TRUE}}, { {
    5, 6, 7, 8}, {
    1.1, 2.1, 3.1, 4.1}, {
    "livido", "src", "gpl", "segfault"}, {
    FALSE, TRUE, FALSE, TRUE}}, { {
    9, 10, 11, 12}, {
    11.11, 12.12, 13.13, 14.14}, {
    "freeze", "unfreeze", "overlay", "multiply"}, {
FALSE, FALSE, FALSE, FALSE}},};

static int array_index = 0;
static int fundemental_index = 0;

int test_fundemental_atoms(livido_port_t * port)
{
    vevo_property_set(port, "int_value", LIVIDO_ATOM_TYPE_INT, 1,
			&(fundementals[fundemental_index].iv));
    vevo_property_set(port, "double_value", LIVIDO_ATOM_TYPE_DOUBLE, 1,
			&(fundementals[fundemental_index].dv));
    vevo_property_set(port, "string_value", LIVIDO_ATOM_TYPE_STRING, 1,
			&(fundementals[fundemental_index].sv));
    vevo_property_set(port, "bool_value", LIVIDO_ATOM_TYPE_BOOLEAN, 1,
			&(fundementals[fundemental_index].bv));
    fundemental_index++;

    return 0;
}

void test_arrays(livido_port_t * port)
{
    vevo_property_set(port, "int_values", LIVIDO_ATOM_TYPE_INT, 4,
			&(arrays[array_index].iv));
    vevo_property_set(port, "double_values", LIVIDO_ATOM_TYPE_DOUBLE, 4,
			&(arrays[array_index].dv));
    vevo_property_set(port, "string_values", LIVIDO_ATOM_TYPE_STRING, 4,
			&(arrays[array_index].sv));
    vevo_property_set(port, "bool_values", LIVIDO_ATOM_TYPE_BOOLEAN, 4,
			&(arrays[array_index].bv));
    array_index++;
}

void test_store_empty_atoms(livido_port_t * port)
{
	vevo_property_set(port, "empty_double", LIVIDO_ATOM_TYPE_DOUBLE, 0 , NULL );

	vevo_property_set(port, "empty_string", LIVIDO_ATOM_TYPE_STRING,0, NULL );

	vevo_property_set(port, "empty_bool", LIVIDO_ATOM_TYPE_BOOLEAN, 0, NULL );

	vevo_property_set(port, "empty_int", LIVIDO_ATOM_TYPE_INT,0, NULL );

	vevo_property_set(port, "empty_array", LIVIDO_ATOM_TYPE_PORTPTR,0,NULL);

}

void dump_empty_atoms(livido_port_t * port)
{
    if (vevo_property_get(port, "ghost", 0, NULL) == LIVIDO_NO_ERROR) {
	printf("\tEmpty property 'ghost' exists\n");
    }
	if( vevo_property_get(port , "empty_string", 0,NULL ) == LIVIDO_NO_ERROR  )
		printf("\tProperty '%s' exists\n", "empty_string" );

	if( vevo_property_get(port , "empty_bool", 0,NULL ) == LIVIDO_NO_ERROR  )
		printf("\tProperty '%s' exists\n", "empty_bool" );

	if( vevo_property_get(port , "empty_int", 0,NULL ) == LIVIDO_NO_ERROR  )
		printf("\tProperty '%s' exists\n", "empty_int" );

	if( vevo_property_get(port , "empty_double", 0,NULL ) == LIVIDO_NO_ERROR  )
		printf("\tProperty '%s' exists\n", "empty_double" );

	int i;
	for(i =0; i < 4; i ++ )
	{
	if( vevo_property_get(port , "empty_array", 0,NULL ) == LIVIDO_NO_ERROR  )
		printf("\tProperty '%s' exists , element %d has size %d\n", "empty_array", 
			i, vevo_property_element_size( port, "empty_array", 0) );
	}

}

void break_here()
{
}
void dump_arrays(livido_port_t * port)
{
    int32_t int_value = 0;
    double double_value = 0.0;
    char *string_value = NULL;
    int32_t bool_value = 0;

    /* allocate space for string list */
    int j;
    for (j = 0; j < 4; j++) {
	vevo_property_get(port, "int_values", j, &int_value);
	printf("\tElement %d of int_values has value %d\n", j, int_value);

	vevo_property_get(port, "double_values", j, &double_value);
	printf("\tElement %d of double_values has value %g\n", j,
	       double_value);

	int ssize = vevo_property_element_size(port, "string_values", j);
	string_value = (char *) malloc(sizeof(char) * ssize);
	vevo_property_get(port, "string_values", j, &string_value);
	printf("\tElement %d of string_values has value '%s'\n", j,
	       string_value);
	free(string_value);

	vevo_property_get(port, "bool_values", j, &bool_value);
	printf("\tElement %d of bool_values has value %d\n", j,
	       bool_value);
    }
}
void dump_port(livido_port_t * port)
{
    int32_t int_value = 0;
    double double_value = 0.0;
    int32_t bool_value = FALSE;
    vevo_property_get(port, "int_value", 0, &int_value);
    vevo_property_get(port, "double_value", 0, &double_value);

    char *string_value =
	(char *)
	malloc(vevo_property_element_size(port, "string_value", 0));
    vevo_property_get(port, "string_value", 0, &string_value);

    vevo_property_get(port, "bool_value", 0, &bool_value);

    printf("\tProperty int_value has value %d\n", int_value);
    printf("\tProperty double_value has value %g\n", double_value);
    printf("\tProperty string value has value '%s'\n", string_value);
    printf("\tProperty bool value has value %d\n", bool_value);

    free(string_value);
}

void dump_ptr_port(livido_port_t * port)
{
    int i;
    for (i = 0; i < 4; i++) {
	uint8_t *plane;
	vevo_property_get(port, "pixeldata", i, &plane);
	printf("\tProperty pixeldata by index %d = %p\n", i, plane);
    }
    for (i = 0; i < 4; i++) {
	void *p = NULL;
	vevo_property_get(port, "ports", i, &p);
	printf("\tProperty ports by index %d = %p\n", i, p);
    }
}

int main(int argc, char *argv[])
{

    void *port = vevo_port_new(0);
    printf("Testing fundementals\n");
    while (fundementals[fundemental_index].sv != NULL) {
	test_fundemental_atoms(port);
	dump_port(port);
    }
    printf("Test voidptr and portptr atom types\n");


    uint8_t *pixel_data[4];
    int i;
    for (i = 0; i < 4; i++)
	pixel_data[i] = (uint8_t *) malloc(sizeof(uint8_t) * 100);

    printf("\tpixel_data %p, %p, %p, %p\n",
	   pixel_data[0], pixel_data[1], pixel_data[2], pixel_data[3]);

    livido_port_t *ports[4] = { port, port, port, port };

    printf("\tport__data %p, %p, %p, %p\n", (void *) port, (void *) port,
	   (void *) port, (void *) port);
    vevo_property_set(port, "pixeldata", LIVIDO_ATOM_TYPE_VOIDPTR, 4,
			&pixel_data);
    vevo_property_set(port, "ports", LIVIDO_ATOM_TYPE_PORTPTR, 4,
			&ports);
    dump_ptr_port(port);

    printf("Testing storing empty atoms of any type\n");

    test_store_empty_atoms(port);
    dump_empty_atoms(port);

    for (i = 0; i < 3; i++) {
	printf("Test arrays of fundementals\n");
	test_arrays(port);
	dump_arrays(port);
    }

    printf("Freeing port %p\n", port);
    printf("Dumping properties in port %p\n", port);

    char **list = vevo_list_properties(port);

    vevo_port_free(port);

    for (i = 0; list[i] != NULL; i++) {
	printf("\tproperty %s\n", list[i]);
	free(list[i]);
    }
    if (list)
	free(list);

    for (i = 0; i < 4; i++)
	free(pixel_data[i]);

    return 1;

}
