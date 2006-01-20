/*
Copyright (c) 1992,1996,1998.  
The Regents of the University of California (Regents).
All Rights Reserved.

Permission to use, copy, modify, and distribute this software and its
documentation for educational, research, and not-for-profit purposes, without
fee and without a signed licensing agreement, is hereby granted, provided that
the above copyright notice, this paragraph and the following two paragraphs
appear in all copies, modifications, and distributions.  Contact The Office of
Technology Licensing, UC Berkeley, 2150 Shattuck Avenue, Suite 510, Berkeley,
CA 94720-1620, (510) 643-7201, for commercial licensing opportunities.

Written by Adrian Freed, The Center for New Music and Audio Technologies,
University of California, Berkeley.

     IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
     SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
     ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF
     REGENTS HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

     REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT
     LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
     FOR A PARTICULAR PURPOSE. THE SOFTWARE AND ACCOMPANYING
     DOCUMENTATION, IF ANY, PROVIDED HEREUNDER IS PROVIDED "AS IS".
     REGENTS HAS NO OBLIGATION TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
     ENHANCEMENTS, OR MODIFICATIONS.
*/

 /* htmsocket.c

	Adrian Freed
 	send parameters to htm servers by udp or UNIX protocol

    Modified 6/6/96 by Matt Wright to understand symbolic host names
    in addition to X.X.X.X addresses.
 */

/*
    mcastsocket.c

    Niels Elburg
    send using multicast protocol

*/

#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/times.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/types.h>

#include <stdlib.h>

#include "mcastsocket.h"           

typedef struct
{
	char	*group_name;
	struct  sockaddr_in	mcast_addr_;
	socklen_t	mcast_addr_len_;
	int		sock_;
} mcast_;               

void *OpenMCASTSocket(const char *mcast_groupname)
{
	mcast_ *mc = (mcast_*) malloc(sizeof( mcast_ ));
	int on = 1;
	unsigned char ttl = 1;
	if(!mc) return NULL;

	memset( &mc->mcast_addr_, 0, sizeof( mc->mcast_addr_ ));
	mc->mcast_addr_len_ = sizeof( struct sockaddr_in );

	mc->group_name = (char*) strdup( mcast_groupname );
	mc->mcast_addr_.sin_addr.s_addr = inet_addr( mcast_groupname );
	mc->mcast_addr_.sin_family = AF_INET;
	mc->mcast_addr_.sin_port = htons( 0 ); // filled on demand
	mc->sock_ = socket( AF_INET, SOCK_DGRAM, 0 );
#ifdef SO_REUSEADDR
	setsockopt( mc->sock_, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
#endif
#ifdef SO_REUSEPORT
	setsockopt( mc->sock_ , SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on));
#endif
	setsockopt( mc->sock_ , IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	return (void*) mc;
}

void	SetInterface( void *handle, const char *ip_address )
{
	struct sockaddr_in if_addr;
	mcast_ *mc = (mcast_*) handle;
	memset( &if_addr, 0, sizeof(if_addr));
	mc->mcast_addr_.sin_addr.s_addr = inet_addr( ip_address );
	mc->mcast_addr_.sin_family = AF_INET;
	setsockopt( mc->sock_, IPPROTO_IP, IP_MULTICAST_IF, &if_addr,
		sizeof( if_addr ) );  
}

int SendMCASTSocket(void *handle, int port, const void *buffer, int len)
{
	mcast_ *mc = (mcast_*) handle;
	mc->mcast_addr_.sin_port = htons( port );
	int n = sendto( mc->sock_, buffer, len, 0,
		(struct sockaddr *) &mc->mcast_addr_, mc->mcast_addr_len_ );
	printf("Send %d bytes \n");
	return n;	 
}
void CloseMCASTSocket(void *handle)
{
	mcast_ *mc = (mcast_*) handle;
	close(mc->sock_);
}
