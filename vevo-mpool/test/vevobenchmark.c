#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <include/libvevo.h>
#include <sys/time.h>
static struct {
    int32_t iv;
    double dv;
    char *sv;
    int32_t *bv;
} fundementals[] = {
    {
    10, 123.123, "parameter value 1", FALSE}, {
    20, 1234567, "parameter value 2", TRUE}, {
    1234, 0.123456, "parameter value 3", FALSE}, {
    65535, 0.999123, "parameter value 4", TRUE}, {
    0, 0.0, NULL, FALSE}
};

static int fundemental_index = 0;

static int stats[2] = { 0, 0 };

int test_fundemental_atoms(livido_port_t * port)
{
    vevo_property_set(port, "int_value", LIVIDO_ATOM_TYPE_INT, 1,
			&(fundementals[fundemental_index].iv));
    vevo_property_set(port, "double_value", LIVIDO_ATOM_TYPE_DOUBLE, 1,
			&(fundementals[fundemental_index].dv));
    vevo_property_set(port, "string_value", LIVIDO_ATOM_TYPE_STRING, 1,
			&(fundementals[fundemental_index].sv));
    vevo_property_set(port, "bool_value", LIVIDO_ATOM_TYPE_INT, 1,
			&(fundementals[fundemental_index].bv));
    fundemental_index++;
    stats[0] += 4;
    return 0;
}

long get_work_size(livido_port_t * port)
{
    long mem_size = 0;
    mem_size += vevo_property_element_size(port, "int_value", 0);
    mem_size += vevo_property_element_size(port, "double_value", 0);
    mem_size += vevo_property_element_size(port, "string_value", 0);
    mem_size += vevo_property_element_size(port, "bool_value", 0);

    return mem_size;
}

void dump_port(livido_port_t * port)
{
    int32_t int_value = 0;
    double double_value = 0.0;
    char *string_value;
    int32_t bool_value = FALSE;


    vevo_property_get(port, "int_value", 0, &int_value);
    vevo_property_get(port, "double_value", 0, &double_value);

    string_value =
	(char *)
	malloc(vevo_property_element_size(port, "string_value", 0));
    vevo_property_get(port, "string_value", 0, &string_value);
    vevo_property_get(port, "bool_value", 0, &bool_value);
    free(string_value);
    stats[1] += 4;
}

int main(int argc, char *argv[])
{
    struct timeval start, end;
    struct timeval tv;
    struct tm *ptm;

    memset(&start, 0, sizeof(struct timeval));
    memset(&end, 0, sizeof(struct timeval));
    memset(&tv, 0, sizeof(struct timeval));


    int max = 100;
    int i = 0;
    int type = 0;
    if (argc >= 2)
	max = atoi(argv[1]);
    if (argc >= 3 )
	type = atoi( argv[2] );
    livido_port_t *port = vevo_port_new(type);

    gettimeofday(&start, NULL);
    for (i = 0; i < max; i++)	// get and set 16 properties per cycle
    {
	/* test 4 parameters, with only fundementals. 1 put and 1 get per property */
	while (fundementals[fundemental_index].sv != NULL) {
	    test_fundemental_atoms(port);
	    dump_port(port);
	}

	fundemental_index = 0;
    }
    gettimeofday(&end, NULL);
    tv.tv_sec = end.tv_sec - start.tv_sec;
    tv.tv_usec = end.tv_usec - start.tv_usec;

    float seconds = (float) tv.tv_sec;
    seconds += (tv.tv_usec / 1000000.0);

    vevo_port_free(port);

    printf("Bench: %ld Atoms put and get in %4.4f seconds\n", (stats[0] + stats[1]), seconds );
    printf("\t %ld Atoms per second\n", (long)( (float) stats[0]/seconds ));

    if (argc <= 1 || argc > 3)
	printf("Hint: use vevobench <number> <0=hash , 1=linked list> \n");
    return 0;
}
