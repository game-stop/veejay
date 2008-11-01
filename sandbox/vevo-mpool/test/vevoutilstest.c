#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>

#include <include/vevo.h>
#include <include/livido.h>

#include <src/vevo-utils.c>

int main(int argc, char *argv[])
{

	livido_port_t *filter = livido_new_filter_class ( 5,
					"Negation",
					"Niels Elburg",
					"Example plugin",
					0,
					"GNU GPL" );

    char **list = livido_list_properties(filter);

    int i;
    for (i = 0; list[i] != NULL; i++) {
	printf("\tproperty %s\n", list[i]);
	free(list[i]);
    }
    if (list)
	free(list);

    livido_port_free( filter );

    return 1;
}
