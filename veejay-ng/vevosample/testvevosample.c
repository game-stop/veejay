#include <stdint.h>
#include <stdio.h>
#include <veejay/defs.h>
#include <libel/vj-el.h>
#include <vevosample/vevosample.h>
#include <libvjmem/vjmem.h>
int main( int argc, char *argv[] )
{
	vj_mem_init();
	vevo_strict_init();
	//@ leaking memory !!!!
	int n = plug_sys_detect_plugins();
	veejay_msg(0,"There are %d Plugins",n);
	plug_sys_init( 0, 352,288 );
	plug_sys_set_palette( 0 );

        samplebank_init();
//	printf("initialized samplebank\n");
	int k = 1;
	int id;
	int q = atoi( argv[1] );
	if( q <= 0 || q > 999998 )
		q = 200;
	for( k = 0; k < q; k ++ )
	{
		void *sample = sample_new(0);
       		int id = samplebank_add_sample(sample);

		sample_fx_set( id, 0, 0 );

		sample_delete( id );
	}


	
	samplebank_free();
	plug_sys_free();
	vevosample_report_stats();
	vevo_report_stats();
//	printf("exit\n");
        return 0;
}

