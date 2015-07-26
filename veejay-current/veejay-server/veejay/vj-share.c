
/*
 * Linux VeeJay
 *
 * Copyright(C)2002-2011 Niels Elburg <nwelburg@gmail.com>
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
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
#include <veejay/vims.h>
#include <libvje/vje.h>
#include <libvjnet/vj-client.h>
#include <veejay/vj-share.h>
#include <veejay/vj-shm.h>
static vj_client	*vj_share_connect(char *hostname, int port)
{
	vj_client *c = vj_client_alloc( 0,0,0 );
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
	unsigned char tmp[64];
	vj_client *c = vj_share_connect( master_host, master_port );
	if(!c) {
		veejay_msg(0, "Error connecting to %s:%d",master_host, master_port );
		return 0;
	}

	memset(tmp,0,sizeof(tmp));

	vj_client_send( c, V_CMD, (unsigned char*)"425:0;" ); 
	
	vj_client_read( c, V_CMD, tmp, 16 ); //@ get SHM id from

	vj_flush(c,1);

//	int32_t key = atoi(tmp);

	int32_t	key = strtol( (char*)tmp, (char**) NULL, 10);

	veejay_msg(VEEJAY_MSG_DEBUG, "Veejay sister at port %d says shared resoure ID is %d",master_port,key);


	vj_client_send( c, V_CMD, (unsigned char*) "025:1;" ); //@ master starts writing frames to shm

	vj_shm_set_id( key ); //@ temporary store

	vj_client_close( c );

	vj_client_free( c );


	return key;
}


