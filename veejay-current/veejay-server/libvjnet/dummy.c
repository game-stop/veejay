/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005 Niels Elburg <nwelburg@gmail.com> 
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
/* 

 very dumb server test code

*/
#include <stdio.h>
#include <string.h>
#include "vj-server.h"

int main( int argc, char *argv[] )
{

	vj_server *s = vj_server_alloc( 5000, NULL, V_CMD);
	vj_server *k = vj_server_alloc( 5001, NULL, V_STATUS );
	int frame = 0;
	int tmplen = 0;

	veejay_set_debug_level(4);

	while(1)
	{
	char status[30];
	int status_len; 
	sprintf(status, "%d %d %d", frame,frame,frame);
	frame++;
	status_len = strlen(status);
	if( vj_server_poll(s) )
	{
		int i;
		if(!vj_server_new_connection( s ))
		{
			int res;
			for(i = 0; i < s->nr_of_links; i ++ )
			{
			res = vj_server_update(s, i );
			if( res == -1 )
			{
				_vj_server_del_client( k, i );
			}

			if( res > 0 )				
			{
				char tmp[4096];
				memset(tmp,0,sizeof(tmp));
				while( vj_server_retrieve_msg(s,i, tmp, &tmplen ) )
				{
					printf("recv %d [%s]\n", tmplen,tmp );
				}
			}
			}
		}
	}
		

	if( vj_server_poll(k) )
	{

		if(!vj_server_new_connection( k ))
		{
			int j ;
			if( k->nr_of_links > 0  )
				for( j = 0; j < k->nr_of_links ; j ++ )
					vj_server_send( k, j, status, status_len );
				
		}
			
	}

	usleep(40000);
	}  
	vj_server_shutdown( s );


	return 0;
}
