/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005 Niels Elburg <nelburg@looze.net> 
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
#include <libvjmsg/vj-common.h>
#include <libvjmem/vjmem.h>
#include "packet.h"

void		packet_dump_header( packet_header_t *h)
{
	veejay_msg(VEEJAY_MSG_DEBUG, "Flag: %x, Sequence Num %x, Timestamp %x:%x Timeout : %ld",
		h->flag, h->seq_num, h->sec, h->usec,h->timeout );
}

packet_header_t		packet_construct_header(uint8_t flag)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	packet_header_t header;
	header.flag = flag;
	header.seq_num = 0;	// not set
	header.sec = tv.tv_sec;
	header.usec = tv.tv_usec;
	header.timeout = 50000;
	return header;
}

packet_header_t		packet_get_header(const void *data)
{
	packet_header_t h;
	veejay_memcpy( &h, data, sizeof(packet_header_t) );
	return h;
}

int			packet_get_data(packet_header_t *h, frame_info_t *i, const void *data, uint8_t *plane )
{
	size_t len = sizeof(packet_header_t);

	if(h->flag)
	{
		veejay_memcpy( i, data + sizeof(packet_header_t) , sizeof( frame_info_t ));
		len += sizeof( frame_info_t );
	}
	
	veejay_memcpy( plane + (CHUNK_SIZE * h->seq_num) , data + len, CHUNK_SIZE );
	return 1;
}

int			packet_put_data(packet_header_t *h, frame_info_t *i , void *payload, const uint8_t *plane )
{
	size_t len = sizeof( packet_header_t );
	
	veejay_memcpy( payload, h , len );
	if(h->flag)
	{
		veejay_memcpy( payload + len, i , sizeof( frame_info_t ));
		len += sizeof(frame_info_t );
	}

	veejay_memcpy( payload + len, plane, CHUNK_SIZE );

	return 1;
}

