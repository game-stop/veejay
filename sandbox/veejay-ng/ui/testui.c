#include <stdio.h>
#include <ui/builder.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <string.h>
#include <glade/glade.h>
#include <ui/uiosc.h>
#include <libvjmem/vjmem.h>
#include <libvevo/libvevo.h>
#include <getopt.h>
#include <unistd.h>

static char *rc_file_ = NULL;
static char *hostname_ = NULL;
static char *portnum_ = NULL;

static	void	usage(const char *progname )
{
	fprintf(stderr,"Usage: %s hostname [portnumber] [options] \n",progname);
	fprintf(stderr,"\tWhere options are:\n");
	fprintf(stderr,"\t-s/--skin /path/to/gtkrc\n");
	fprintf(stderr,"\n\n");
	exit(1);		
}

static	int	set_option(const char name, char *value)
{
	int nerr = 0;
	if( strcmp(name, "skin" ) == 0 || strcmp(name, "s" ) == 0  )
		rc_file_ = strdup( value );
	else
		nerr++;

	return nerr;
}

static	int	check_command_line_options( int argc, char *argv[])
{
	int nerr,n,option_index = 0;
	char option[2];
#ifdef HAVE_GETOPT_LONG
	static struct option long_options[] = {
		{"skin",1,0,0},
		{NULL,0,0,0}
	};
#endif
	if( argc < 2 )
		usage(argv[0]);
	nerr = 0;
#ifdef HAVE_GETOPT_LONG
	while(( n = getopt_long( argc, argv, "s:p:h:", long_options, &option_index)) != EOF )
#else
	while(( n = getopt( argc, argv, "s:p:h:")) != EOF )
#endif
	{
		switch(n) 
		{
#ifdef HAVE_GETOPT_LONG
			case 0:
				nerr += set_option( long_options[option_index].name, optarg );
				break;
#endif
		default:
				sprintf(option, "%c", n );
				nerr += set_option(option,optarg);
				break;
		}
	}
	if( optind > argc || argc <= 1 )
		nerr++;
	if(nerr)
		usage(argv[0]);
	if(!nerr)
		return 1;
	return 0;
}

int main(int argc, char *argv[])
{
	//@ initialize a window with a frame, vbox ( label, buttonbar )
    	vj_mem_init();
    	vevo_strict_init();

	gtk_init(NULL,NULL );
	glade_init();

	if(argv[1])
		hostname_ = strdup( argv[1]);
	if(argv[2])
		portnum_ = strdup( argv[2]);
	
	if(!hostname_)
		usage( argv[0]);

	if(!check_command_line_options(argc, argv))
    	{
		return 0;
	}


	if( rc_file_ )
		gtk_rc_parse( rc_file_ );

	veejay_msg(0, "Connecting to %s:%s",hostname_,
			(portnum_==NULL ? "3490" : portnum_ ));

        void *serv = ui_new_osc_server(NULL, "5005");


	director_init( hostname_,
			(portnum_==NULL ? "3490" : portnum_) );

	gtk_main();

	return 1;	
}
