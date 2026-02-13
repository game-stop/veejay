
/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2015 Niels Elburg <nwelburg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License , or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 */


#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <veejaycore/defs.h>
#include <veejaycore/vjmem.h>
#include <veejaycore/vj-msg.h>
#include <veejaycore/vims.h>
#include <libvje/vje.h>
#include <veejaycore/vj-client.h>
#include <veejay/vj-share.h>
#include <veejay/vj-shm.h>
static vj_client	*vj_share_connect(char *hostname, int port)
{
	vj_client *c = vj_client_alloc();
	if(!c) return NULL;
	int res = 0;
	if( hostname == NULL )
		res = vj_client_connect( c, "127.0.0.1", NULL, port );
	else
		res = vj_client_connect( c, hostname, NULL, port );
	if(!res)  {
		vj_client_free(c);
		c = NULL;
	}

	return c;
}
static void vj_flush(vj_client *sayvims,int frames) { 
        unsigned char status[128];
        memset(status,0,sizeof(status));

        while(frames>0) {
                if( vj_client_poll(sayvims, V_STATUS ))
                {
                        uint8_t sta_len[6];
                        memset(sta_len,0,sizeof(sta_len));
                        int nb = vj_client_read( sayvims, V_STATUS, sta_len, 5 );
                        if( nb <= 0 )
							break;
						if(sta_len[0] == 'V' )
                        {
                                int bytes = 0;
                                sscanf( (char*) sta_len + 1, "%03d", &bytes );
                                if(bytes > 0 )
                                {
                                        memset(status,0,sizeof(status));
                                        int n = vj_client_read(sayvims,V_STATUS,status,bytes);
                                        if( n )
                                        {
                                                frames -- ;
                                        }
										if( n<= 0)
											break;
								}
                        }
                }
        }
}

int32_t			vj_share_pull_master( void *shm, char *master_host, int master_port )
{
	char tmp[64];
	vj_client *c = vj_share_connect( master_host, master_port );
	if(!c) {
		veejay_msg(0, "Error connecting to %s:%d",master_host, master_port );
		return 0;
	}

	memset(tmp,0,sizeof(tmp));

	vj_client_send( c, V_CMD, (unsigned char*)"425:0;" ); 
	
	vj_client_read( c, V_CMD, (unsigned char*) tmp, 16 ); //@ get SHM id from

	vj_flush(c,1);

//	int32_t key = atoi(tmp);

	int32_t	key = strtol( (char*)tmp, (char**) NULL, 10);

	veejay_msg(VEEJAY_MSG_DEBUG, "Veejay sister at port %d says shared resource ID is %d",master_port,key);


	vj_client_send( c, V_CMD, (unsigned char*) "025:1;" ); //@ master starts writing frames to shm

	vj_shm_set_id( key ); //@ temporary store

	vj_client_close( c );

	vj_client_free( c );


	return key;
}

int	vj_share_get_info( char *host, int port, int *width, int *height, int *format, int *key, int screen_id )
{
	char tmp[128];
	vj_client *c = vj_share_connect( host, port );
	if(!c) {
		veejay_msg(0, "Error connecting to %s:%d",host,port );
		return 0;
	}

	snprintf(tmp,sizeof(tmp),"%03d:%d;", VIMS_GET_SHM_EXT, screen_id );
	vj_client_send( c, V_CMD, (unsigned char*) tmp );

	memset(tmp,0,sizeof(tmp));
	vj_client_read( c, V_CMD, (unsigned char*) tmp, 3 ); //@ get SHM id from

	int msg_len = 0;
	if( sscanf( tmp, "%d", &msg_len ) ) {

		vj_client_read( c, V_CMD, (unsigned char*) tmp, msg_len );

		sscanf( tmp, "%d %d %d %d",
				width,height,format,key );

		veejay_msg(VEEJAY_MSG_DEBUG, "Veejay %s:%d has a shared memory resource at %x (%d) in %dx%d@%d", host,port, *key,*key, *width,*height,*format);
	}
	vj_client_close( c );

	vj_client_free( c );

	return 1;
}


int	vj_share_start_slave( char *host, int port, int shm_id)
{
	char tmp[64];
	vj_client *c = vj_share_connect( host,port );
	if(!c) {
		veejay_msg(0, "Error connecting to %s:%d",host, port );
		return 0;
	}

	snprintf( tmp,sizeof(tmp), "%03d:%d;", VIMS_SPLIT_CONNECT_SHM, shm_id );

	vj_client_send( c, V_CMD, (unsigned char*) tmp ); 

	vj_client_close( c );

	vj_client_free( c );

	return 1;
}

int	vj_share_start_net( char *host, int port, char *master_host, int master_port)
{
	char tmp[64];
	vj_client *c = vj_share_connect( host,port );
	if(!c) {
		veejay_msg(0, "Error connecting to %s:%d",host, port );
		return 0;
	}

	snprintf( tmp,sizeof(tmp), "%03d:%d %s;",VIMS_STREAM_NEW_UNICAST,master_port, master_host );

	vj_client_send( c, V_CMD, (unsigned char*) tmp ); 

	vj_client_close( c );

	vj_client_free( c );

	return 1;
}

int	vj_share_play_last( char *host, int port )
{
	char tmp[64];
	vj_client *c = vj_share_connect( host,port );
	if(!c) {
		veejay_msg(0, "Error connecting to %s:%d",host, port );
		return 0;
	}

	snprintf( tmp,sizeof(tmp), "%03d:-1;",VIMS_STREAM_SELECT );

	vj_client_send( c, V_CMD, (unsigned char*) tmp ); 

	vj_client_close( c );

	vj_client_free( c );

	return 1;
}

