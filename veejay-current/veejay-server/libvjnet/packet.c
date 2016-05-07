/* vjnet - low level network I/O for VeeJay
 *
 * (C) 2005-2016 Niels Elburg <nwelburg@gmail.com> 
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
#include <stdint.h>
#include <libvjmsg/vj-msg.h>
#include <libvjmem/vjmem.h>
#include "packet.h"

void		packet_dump_header( packet_header_t *h)
{
	veejay_msg(VEEJAY_MSG_DEBUG, "Flag: %x, Sequence Num %d/%d, Timestamp %ld Timeout : %d",
		h->seq_num,h->length, h->usec,h->timeout );
}

packet_header_t		packet_construct_header(uint8_t flag)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	packet_header_t header;
	header.seq_num = 0;
	header.usec = tv.tv_usec;
	header.timeout = 0;
	header.length = 0;
	return header;
}

packet_header_t		packet_get_header(const void *data)
{
	packet_header_t h,tmp;
	veejay_memcpy( &tmp, data, PACKET_HEADER_LENGTH );
	h.seq_num = tmp.seq_num;
	h.length = tmp.length;
	h.usec = tmp.usec;
	h.timeout = tmp.timeout;
	return h;
}

packet_header_t  *packet_get_hdr(const void *data)
{
	return (packet_header_t*) data;
}

int			packet_get_data(packet_header_t *h, const void *data, uint8_t *plane )
{
	uint8_t *addr = (uint8_t*) data;
	veejay_memcpy( plane , addr + PACKET_HEADER_LENGTH, CHUNK_SIZE );
	return 1;
}

int			packet_put_padded_data(packet_header_t *h, void *payload, const uint8_t *plane, int bytes )
{
	uint8_t *dst = (uint8_t*) payload;
	size_t len = PACKET_HEADER_LENGTH;
	veejay_memcpy( dst, h , len );
	veejay_memcpy( dst + len, plane, bytes );
	return (len + bytes);
}

int			packet_put_data(packet_header_t *h, void *payload, const uint8_t *plane )
{
	uint8_t *dst = (uint8_t*) payload;
	veejay_memcpy( dst, h , PACKET_HEADER_LENGTH );
	veejay_memcpy( dst + PACKET_HEADER_LENGTH, plane, CHUNK_SIZE );
	return 1;
}

