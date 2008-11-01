#include <stdio.h>
#include "../include/vevo.h"
#include "../include/livido.h"


int main(int argc, char *argv[])
{
	char *str = "123412341234123412341asdfasdfasdf";

	char *bogus = (char*) malloc( 100);
	int t1,t2;
	int a,b;
	double c,d;
	int e,f;
	livido_port_t *port = livido_port_new( 0 );
	if( livido_property_get( port, "type", 0, NULL ) == 0 )
	{
		if( livido_property_atom_type(port, "type" ) != LIVIDO_ATOM_TYPE_INT )
		 printf("Not a livido plugin\n");
	}

	t1 = livido_property_set( port, "test", LIVIDO_ATOM_TYPE_STRING, 1, &str );
	t2 = livido_property_get( port, "test", 0, &bogus );

	a = 100;
	b = 0;
	t1 = livido_property_set( port, "testo", LIVIDO_ATOM_TYPE_INT, 1, &a );
	t2 = livido_property_get( port, "testo", 0, &b );


	c = 100.123;
	d = 0;
	t1 = livido_property_set( port, "testd", LIVIDO_ATOM_TYPE_DOUBLE, 1, &c );
	t2 = livido_property_get( port, "testd", 0, &d );

	c= 99.872;
	t1 = livido_property_set( port, "testd", LIVIDO_ATOM_TYPE_DOUBLE, 1, &c );
	t2 = livido_property_get( port, "testd", 0, &d );

	e = 100;
	f = 0;
	t1 = livido_property_set( port, "testb", LIVIDO_ATOM_TYPE_BOOLEAN, 1, &e );
	t2 = livido_property_get( port, "testb", 0, &f );


	uint8_t *uu = (uint8_t*) malloc(sizeof(uint8_t) * 10);
	uint8_t *dst = NULL;
	livido_property_set( port, "ptr", LIVIDO_ATOM_TYPE_VOIDPTR, 1, &uu );
	livido_property_get( port, "ptr", 0, &dst );

	printf("Values %s, %d, %g, %d, %p=%p\n", bogus,a,c ,f,uu,dst);
	livido_port_free( port );
	free(bogus);
	free( uu );
	return 0;
}
