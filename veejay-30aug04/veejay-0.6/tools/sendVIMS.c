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

typedef struct {
	struct hostent *he;
	struct sockaddr_in server_addr;
	int handle;
} veejay;

static veejay *status_socket;
static veejay *command_socket;

static char  hostname[512];
static int   port_number = 3490;
static int   use_file = 0;
static char  filename[1024];

/* make a connection with a server 'name' at port 'port_id' */
static int vj_connect( veejay *v, char *name, int port_id ) {
	v->he = gethostbyname(name);
	v->handle = socket( AF_INET, SOCK_STREAM, 0);
	v->server_addr.sin_family = AF_INET;
	v->server_addr.sin_port = htons(port_id);
	v->server_addr.sin_addr = *( (struct in_addr*) v->he->h_addr);
	if(connect(v->handle, (struct sockaddr*) &v->server_addr,sizeof(struct sockaddr))==-1) {
		return -1; /* error */
	}
	return 0;
}

/* read a line of status information */
static int vj_status_read() {
	int nbytes,n;
	char status[1024];
	nbytes = 100;
	n = recv( status_socket->handle, status, nbytes,0);
	return n;
}

/* count played frames (delay) */
static void vj_flush(int frames) { 
	int n = 0;
	while(frames!=0) {
		if(vj_status_read()>0) frames--; 
	}
}


/* send a message to veejay */
static void vj_send(char *buf) {
   char buf2[100];
   int len = 0;
   sprintf(buf2, "V%03dD%s",strlen(buf),buf);
   len = strlen(buf2);    
   if ((send(command_socket->handle, buf2, len, 0)) == -1)
   { /* send the command */
            perror("send");
            exit(1);
   }
}

static void Usage(char *progname)
{
	fprintf(stderr, "Usage: %s [options] [messages]\n",progname);
	fprintf(stderr, "where options are:\n");
	fprintf(stderr, " -p\t\tVeejay port (3490)\n"); 
	fprintf(stderr, " -h\t\tVeejay host (localhost)\n");
	fprintf(stderr, " -f <filename>\tSend contents of this file to veejay\n\n");
	fprintf(stderr, "Messages to send to veejay must be wrapped in quotes\n");
	fprintf(stderr, "You can send multiple messages by seperating them with a whitespace\n");  
	fprintf(stderr, "\n");
	fprintf(stderr, "Example: %s 255:;\n");
	fprintf(stderr, "         (quit veejay)\n\n");
	fprintf(stderr, "	  %s +100 255:;\n");
	fprintf(stderr, "	  (quit veejay after playing 100 frames)\n");
	exit(-1);
}

static int set_option(const char *name, char *value)
{
	int err = 0;
	if(strcmp(name, "h") == 0)
	{
		
	}
	else if (strcmp(name, "p") == 0)
	{
		port_number = atoi(optarg);
	}
	else if (strcmp(name, "f") == 0)
	{
		use_file = 1;
		strcpy( filename, optarg );
	}
	else err++;

	return err;
}

static char buf[65535];
int main(int argc, char *argv[])
{
       	int i;
	int n = 0;
	int x = 0;
	int k =0;
	char msg[20];
	command_socket = (veejay*)malloc(sizeof(veejay));
	status_socket = (veejay*)malloc(sizeof(veejay));
	char option[2];
	int err = 0;
	sprintf(hostname,"%s", "localhost");

	while( ( n = getopt(argc,argv, "h:p:f:")) != EOF)
	{
		sprintf(option,"%c",n);
		err += set_option( option,optarg);
	}
	if(argc <= 1 )
		Usage(argv[0]);

	if( optind > argc )
		err ++;
	if ( err )
		Usage(argv[0]);



	bzero( buf, 65535 );

	/* setup the connection with veejay */
        if (vj_connect( command_socket, hostname, port_number ) == -1) {  // get the host info 
	    fprintf(stderr, "cannot connect to veejay\n");
            exit(1);
        }
	/* open status */
	if (vj_connect( status_socket, hostname, port_number+1 ) == -1) {
		fprintf(stderr, "cannot connect to status socket\n");
		exit(1);
	}

	if(use_file)
	{
		FILE *fd = fopen( filename ,"r");
		while( fgets(buf,4096,fd) )
		{
			if(buf[0]=='+')
			{
				int delay=1;	
				sscanf(buf+1,"%d",&delay);
				vj_flush(delay);
			}
			else
			{
				if(buf[0]!='#') {
					buf[strlen(buf)-1] = '\0';
					vj_send(buf);
				}
			}	
		}
		fclose(fd);
	}
	else
	{
		char **msg = argv + optind;
		int  nmsg  = argc - optind;
		int i=0;
		while( i < nmsg )
		{
			if(msg[i][0] == '+')
			{
				int delay = 1;
				char *tmp = msg[i];
				sscanf(tmp + 1, "%d",&delay);
				vj_flush(delay);
			}
			else
			{
				vj_send( msg[i] );
			}
			i++;
		}
	}

        close(status_socket->handle);
	close(command_socket->handle);

        return 0;
} 
