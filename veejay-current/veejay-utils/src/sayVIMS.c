/* sendVIMS - very simple client for VeeJay
 * 	     (C) 2002-2015 Niels Elburg <nwelburg@gmail.com> 
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
#include <config.h>
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
static int interactive = 0;
static int port_num = 3490;
static char *group_name = NULL;
static char *host_name = NULL;
static vj_client *sayvims = NULL;
static int single_msg = 0;
static int dump = 0;
static int verbose = 0;
static int base64_encode = 0;
static int help = 0;

#ifdef BASE64_AVUTIL
#include <libavutil/base64.h>
#endif

#define SHM_ID_LEN 16

/* add 4xx VIMS series, read back data from blocking socket */
static struct {
	int vims;
	int hdr; // make header lengths consistent one day ...
} vims_replies[] = {
    { VIMS_VIDEO_INFORMATION,3 }, 
	{ VIMS_EFFECT_LIST,6 },            
	{ VIMS_EDITLIST_LIST,6 },    
    { VIMS_BUNDLE_LIST,6 },          
	{ VIMS_STREAM_LIST,5 },
	{ VIMS_SAMPLE_LIST, 8},
	{ VIMS_STREAM_GET_V4L,3},
	{ VIMS_CHAIN_GET_ENTRY,3},
	{ VIMS_VIMS_LIST,5},
	{ VIMS_SAMPLE_INFO,8},
	{ VIMS_SAMPLE_OPTIONS,5},
	{ VIMS_DEVICE_LIST,6},
	{ VIMS_FONT_LIST,6},
	{ VIMS_SRT_LIST,6},
	{ VIMS_SRT_INFO,6},
	{ VIMS_TRACK_LIST,5},
	{ VIMS_SEQUENCE_LIST,6},
	{ VIMS_KEYLIST,6},
	{ VIMS_WORKINGDIR,8},
	{ VIMS_SAMPLE_STACK,3},
	{ VIMS_GET_IMAGE,8},
	{ VIMS_CHAIN_GET_PARAMETERS,4},
	{ VIMS_GET_SHM,SHM_ID_LEN },
	{ 0,0 },
};

static vj_client	*sayvims_connect(void)
{
	vj_client *client = vj_client_alloc( 0,0,0 );
	if(!client)
	{
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

static	void reconnect(void)
{
	if( sayvims ) {
		vj_client_close(sayvims);
		vj_client_free(sayvims);
	}

	sayvims = sayvims_connect();
	if( sayvims == NULL ) {
		fprintf(stderr, "Unable to make a connection with %s:%d\n", host_name, port_num);
		exit(1);
	}
	
//	fprintf(stdout, "Connected to %s:%d\n", host_name,port_num);
}

static int vjsend( int cmd, unsigned char *buf )
{
	/* bad-check if connection is still up */
	int foobar = vj_client_poll(sayvims, V_CMD );
	if( foobar && vj_client_link_can_read(sayvims, V_CMD) ) {
		unsigned char dummy[8];
		/* read one byte will fail if connection is closed */
		/* nb: only vims query messages write something to V_CMD (vims 400-499)*/
		int res = vj_client_read(sayvims, V_CMD, dummy, 1);
		if( res <= 0 ) { 
			reconnect();
		}
	}

	/* strip newline */
	size_t index = strcspn( (char*) buf,"\n\r");
	if (index) {
		buf[ index ] = '\0';
	}
	
	if( index == 0 )
		return 1;

	/* send buffer */
	int result = vj_client_send( sayvims,cmd, buf);
    if( result <= 0) {
		fprintf(stderr, "Unable to send message '%s'\n", buf );
		return 0;
	}

	return 1;
}

static int vimsReplyLength(int vims_id)
{
	int i;
	for( i = 0; vims_replies[i].vims != 0; i ++ ) {
		if( vims_replies[i].vims == vims_id ) {
			return vims_replies[i].hdr;
		}
	}
	return 0;
}

static unsigned char *vimsReply(int expectedLen, int *actualWritten)
{
	if( expectedLen <= 0 )
		return NULL;

	int hdrLen = expectedLen + 1;
	char header[hdrLen];

	memset( header, 0, sizeof(header) );

	int result = vj_client_read( sayvims, V_CMD, (unsigned char*) header, expectedLen );
	if( result == -1 ) {
		return NULL;
	}

	int dataLen = 0;
	if( sscanf( header, "%d", &dataLen ) != 1 ) {
		return NULL;
	}

	unsigned char *data = NULL;
	if( result <= 0 || dataLen <= 0 || expectedLen <= 0 )
		return data;

	data = (unsigned char*) vj_calloc( sizeof(unsigned char) * (dataLen + 1));
	*actualWritten = vj_client_read( sayvims, V_CMD, data, dataLen );
	if( *actualWritten == -1 ) {
		return NULL;
	}
	return data;
}

static unsigned char *vimsAnswer(int len)
{
	unsigned char *tmp = (unsigned char*) vj_calloc( sizeof(unsigned char) * (len+1));
	int result = vj_client_read( sayvims, V_CMD, tmp, len );
	if( result == -1 ) {
		free(tmp);
		tmp = NULL;
	}
	return tmp;
}

static int vimsMustReadReply(char *msg, int *vims_event_id)
{
	int mustRead = 0;
	int vims_id = 0;
	if( sscanf( msg, "%d:", &vims_id ) ) {
		if( vims_id >= 400 && vims_id <= 499 ) {
			mustRead = 1;
		}
	}
	*vims_event_id = vims_id;

	return mustRead;
}

/* count played frames (delay) */
static int vj_flush(int frames) { 
	
	char status[255];
	memset(status,0,sizeof(status));

	while(frames>0) {
		if( vj_client_poll(sayvims, V_STATUS ))
		{
			char sta_len[6];
			memset( sta_len,0,sizeof(sta_len));
			int nb = vj_client_read( sayvims, V_STATUS, (unsigned char*) sta_len, 5 );
			if( nb <= 0 )
				return 0;

			if(sta_len[0] == 'V' )
			{
				int bytes = 0;
				sscanf( sta_len + 1, "%03d", &bytes );
				if(bytes > 0 )
				{
					memset( status,0, sizeof(status));
					int n = vj_client_read(sayvims,V_STATUS,(unsigned char*) status,bytes);
					if( n )
					{
						if(dump) fprintf(stdout , "%s\n", status );
						frames -- ;
					} 
					else if(n == -1)
					{
						fprintf(stderr, "Error reading status from Veejay\n");	
						return 0;
					}
				}
			}
		}
	}
	return 1;
}

static int processLine(FILE *infile, FILE *outfile, char *tmp, size_t len)
{
	int line_len = 0;
		
	memset( tmp, 0, len + 1 );
		
	line_len = getline( &tmp, &len, infile );

	if( line_len > 0 ) {
		tmp[line_len] = '\0';

		if( strncmp( "quit", tmp,4 ) == 0 ) {
			return -1;
		}

		if( tmp[0] == '+' ) {
			int wait_frames_ = 1;
			if( sscanf( tmp + 1, "%d" , &wait_frames_ ) == 1 ) {
				if( vj_flush( wait_frames_ ) == 0)
					return 0;
			} 
		} else {
			int vims_id = 0;
			int mustRead = vimsMustReadReply( tmp, &vims_id );

			if( vjsend( V_CMD, (unsigned char*) tmp ) == 0 ) {
				return 0;
			}

			if( mustRead ) {
				if( vims_id == VIMS_GET_SHM ) {
					unsigned char *data = vimsAnswer( SHM_ID_LEN );
					if( data != NULL ) {
						fwrite( data, sizeof(unsigned char), SHM_ID_LEN, outfile);
						free(data);
					}
				} else if ( vims_id == VIMS_GET_IMAGE ) {
					int headerLength = vimsReplyLength( vims_id );
					int dataLength = 0;
					unsigned char *data = vimsReply( headerLength, &dataLength);
					char *out = NULL;
					if( data != NULL ) {
						if( base64_encode ) {
#ifdef BASE64_AVUTIL
							int len = AV_BASE64_SIZE( dataLength );
							out = (char*) vj_calloc( sizeof(char) * len );
							char *b64str = av_base64_encode( out, len, data, dataLength );
							if( b64str != NULL ) {
								fwrite( b64str, sizeof(char), len, outfile);
							}
#else
							fwrite(data,sizeof(char),dataLength,outfile);
#endif
						} else {
							fwrite( data, sizeof(char), dataLength, outfile);
						}
						if( out != NULL ) free(out);
						free(data);
					}	
				} else {
					int headerLength = vimsReplyLength( vims_id );
					int dataLength = 0;
					unsigned char *data = vimsReply( headerLength, &dataLength);
					if( data != NULL ) {
						fwrite( data, sizeof(unsigned char), dataLength, outfile);
						free(data);
					}
					
				}
				fflush(outfile);
		 	}

		}
	} else if (line_len < 0) {
		return 0; // exit
	}

	return 1; //wait for more input 
}


static void Usage(char *progname)
{
	fprintf(stderr, "Usage: %s [options] [messages]\n",progname);
	fprintf(stderr, "where options are:\n");
	fprintf(stderr, " -p\t\tVeejay port (3490)\n"); 
	fprintf(stderr, " -g\t\tVeejay groupname (224.0.0.31)\n");
	fprintf(stderr, " -h\t\tVeejay hostname (localhost)\n");
	fprintf(stderr, " -m\t\tSend single message\n");
	fprintf(stderr, " -i\t\tInteractive mode\n");
	fprintf(stderr, " -d\t\tDump status to stdout\n");
	fprintf(stderr, " -b\t\tBase64 encode binary data\n");
	fprintf(stderr, " -v\t\tVerbose\n");
	fprintf(stderr, " -?\t\tPrint this help\n");
	fprintf(stderr, "\nExit interactive mode by typing 'quit'\n");
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
	else if(strcmp(name,"b") == 0 )
	{
#ifdef BASE64_AVUTIL
		base64_encode = 1;
#else
		fprintf(stderr, "Compiled without base64 support\n");
		err++;
#endif
	}
	else if(strcmp(name,"?") == 0)
	{
		help = 1;
	}
	else err++;

	return err;
}

int main(int argc, char *argv[])
{
	int i = 0;
	int n = 0;
	char option[2];
	int err = 0;

	veejay_set_debug_level(verbose);

	// parse commandline parameters
	while( ( n = getopt(argc,argv, "h:g:p:midbv?")) != EOF)
	{
		sprintf(option,"%c",n);
		err += set_option( option,optarg);
	}

	if(help || err  || optind > argc)
	{
		Usage( argv[0] );
		return -1;
	}

	vj_mem_init();

	reconnect();

	if(!sayvims) {
		return -1;
	}

	if(single_msg)  /* single message send */
	{
		char **msg = argv + optind;
		int  nmsg  = argc - optind;
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
					fprintf(stderr, "Unable to parse wait period '%s'\n", tmp );
					goto end_program;
				}
			}
			else
			{
				if( vjsend( V_CMD, (unsigned char*)msg[i] ) == 0 )
				{
					goto end_program;
				}
			}

			i++;
		}

		vj_flush(1);
	}
	else if(interactive) /* interactive mode, reconnect on error */
	{
		const unsigned int len = 1024;
		fprintf(stdout, "veejay sayVIMS %s\n",VERSION);
		fprintf(stdout, "\ttype 'quit' to exit\n");
		fprintf(stdout, "\ttype 'veejay -u' to see a list of veejay commands\n");
		for( ;; ) {
			fprintf(stdout, "> " );
		
			char *tmp = (char*) vj_calloc( len + 1 ); // max line length
			int result = processLine(stdin,stdout, tmp, len);
			if( result == -1 ) {
				fprintf(stderr, "User requested session end, bye!\n");
				break;
			}
			free(tmp);
		}
	}
	else
	{	
		/* interactive silent mode if no parameters given (pipe through) */
		int fd_in = 0;
		FILE *instd = fdopen(fd_in,"r");
		if(instd == NULL ) {
			fprintf(stderr, "Cannot read from STDIN\n");
			return 1;
		}

		char buf[1024];
		while( fgets( buf, sizeof(buf), instd ) ) {
			if( buf[0] == '+' ) {
				int wait_ = 1;
				if( sscanf( buf+1,"%d",&wait_)==1) {
					vj_flush(wait_);
				}
			}
			else {
				vjsend( V_CMD, (unsigned char*)buf );
			}
		}

		vj_flush(1);
	}

end_program:
	vj_client_close(sayvims);
	vj_client_free(sayvims);
	free(host_name);

    return 0;
} 
