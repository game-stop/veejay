/* sendVIMS - very simple client for VeeJay
 * 	     (C) 2002-2004 Niels Elburg <elburg@hio.hen.nl> 
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <veejay/vims.h>
#include <veejay/vj-client.h>
#include <veejay/vjmem.h>
#include <veejay/vj-msg.h>
static int   interactive = 0;
static int   port_num = 3490;
static char  *group_name = NULL;
static char  *host_name = NULL;
static vj_client *sayvims = NULL;
static int colors = 0;
static int fd_in = 0; // stdin
static int single_msg = 0;
static int dump = 0;
static int verbose = 0;
/* count played frames (delay) */
static void vj_flush(int frames) { 
	
	char status[100];
	bzero(status,100);

	while(frames>0) {
		if( vj_client_poll(sayvims, V_STATUS ))
		{
			char sta_len[6];
			bzero(sta_len, 6 );
			int nb = vj_client_read( sayvims, V_STATUS, sta_len, 5 );
			if(sta_len[0] == 'V' )
			{
				int bytes = 0;
				sscanf( sta_len + 1, "%03d", &bytes );
				if(bytes > 0 )
				{
					bzero(status, 100);
					int n = vj_client_read(sayvims,V_STATUS,status,bytes);
					if( n )
					{
						if(dump) fprintf(stdout , "%s\n", status );
						frames -- ;
					}
					if(n == -1)
					{
						fprintf(stderr, "Error reading status from Veejay\n");	
						exit(0);
					}
				}
			}
		}
	}
}

static void Usage(char *progname)
{
	fprintf(stderr, "Usage: %s [options] [messages]\n",progname);
	fprintf(stderr, "where options are:\n");
	fprintf(stderr, " -p\t\tVeejay port (3490)\n"); 
	fprintf(stderr, " -g\t\tVeejay groupname (224.0.0.31)\n");
	fprintf(stderr, " -h\t\tVeejay hostname (localhost)\n");
	fprintf(stderr, " -m\t\tSend single message\n");
	fprintf(stderr, " -c\t\tColored output\n");
	fprintf(stderr, " -d\t\tDump status to stdout\n");
	fprintf(stderr, " -v\t\t\n");
	fprintf(stderr, "Messages to send to veejay must be wrapped in quotes\n");
	fprintf(stderr, "You can send multiple messages by seperating them with a whitespace\n");  
	fprintf(stderr, "Example: %s \"600:;\"\n",progname);
	fprintf(stderr, "         (quit veejay)\n");
	fprintf(stderr, "Example: echo \"%03d:;\" | %s \n", VIMS_QUIT, progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Example: sayVIMS -h 192.168.100.12 -m \"600:;\"\n");
}

static int set_option(const char *name, char *value)
{
	int err = 0;
	if(strcmp(name, "h") == 0 )
	{
		host_name = strdup(optarg);
		if(group_name) err ++;
	}
	else if(strcmp(name, "g") == 0)
	{
		if(host_name) err ++; 
		group_name = strdup( optarg );	
	}
	else if (strcmp(name, "p") == 0)
	{
		port_num = atoi(optarg);
	}
	else if (strcmp(name, "i") == 0)
	{
		interactive = 1;
	}
	else if (strcmp(name, "c") == 0)
	{	
		colors = 1;
	}
	else if (strcmp(name, "m") == 0 )
	{
		single_msg = 1;
	}
	else if(strcmp(name, "d") == 0)
	{
		dump = 1;
	}
	else if(strcmp(name,"v") == 0 )
	{
		verbose = 1;
	}
	else err++;

	return err;
}

vj_client	*sayvims_connect(void)
{
	vj_client *client = vj_client_alloc( 0,0,0 );
	if(!client)
	{
		fprintf(stderr, "Memory allocation error\n");
		return NULL;
	}

	if(host_name == NULL)
		host_name = strdup( "localhost" );

	if(!vj_client_connect( client, host_name,group_name, port_num ))
	{
		fprintf(stderr,"Unable to connect to %s:%d\n", host_name, port_num );
		return NULL;
	}
	
	return client;
}

int main(int argc, char *argv[])
{
       	int i;
	int n = 0;
	char option[2];
	int err = 0;
	FILE *infile;

	veejay_set_debug_level(verbose);

	// parse commandline parameters
	while( ( n = getopt(argc,argv, "h:g:p:micdv")) != EOF)
	{
		sprintf(option,"%c",n);
		err += set_option( option,optarg);
	}

	if( err  || optind > argc)
	{
		Usage( argv[0] );
		return -1;
	}

	vj_mem_init();

	veejay_set_debug_level( verbose );

	sayvims = sayvims_connect();

	if(!sayvims) {
		fprintf(stderr, "error connecting.\n");
		return -1;
	}

	if(single_msg || interactive )
	{
		char **msg = argv + optind;
		int  nmsg  = argc - optind;
		i=0;
		while( i < nmsg )
		{
			if(msg[i][0] == '+')
			{
				int delay = 1;
				char *tmp = msg[i];
				if(sscanf(tmp + 1, "%d",&delay) == 1 )
				{
					vj_flush(delay);
				}
				else
				{
					fprintf(stderr, "Fatal: error parsing %s\n", tmp );
					return -1;
				}
			}
			else
			{
				vj_client_send( sayvims,V_CMD, msg[i] );
			}
			i++;
		}
	}
	else
	{
		/* read from stdin*/
		infile = fdopen( fd_in, "r" );
		if(!infile)
		{
			fprintf(stderr, "Cannot read from STDIN\n");
			return 0;
		}	
		char buf[128];
		while( fgets(buf, 100, infile) )
		{
			if( buf[0] == '+' )
			{
				int wait_ = 1;
		
				if(sscanf( buf+1, "%d", &wait_ ) )
				{
					vj_flush( wait_ );
				}
				else
				{
					fprintf(stderr,"Delay not valid: '%d'\n", wait_ );
				}
			}
			else
			{
				vj_client_send( sayvims, V_CMD, buf );
			}
		}
	}



	vj_flush(1);
	vj_client_close(sayvims);
	vj_client_free(sayvims);

        return 0;
} 
