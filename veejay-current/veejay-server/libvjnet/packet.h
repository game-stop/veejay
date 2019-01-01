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

#ifndef PACKET_H
#define PACKET_H
/*

Veejay - Video streaming over UDP (Multicast)

Veejay doesnt care about dropped packet. Dropped packet means drop frame.

Ignoring machine byte order. Fix it yourself

	
*/
#include	<stdio.h>
#include	<sys/time.h>
#include 	<stdint.h>

typedef struct
{
	uint16_t	seq_num;   /* sequence number */
	long		usec;	   /* time stamp */
	uint16_t	length;    /* total number */
	uint16_t	data_size; /* number of data bytes, can be smaller than CHUNK_SIZE */
} packet_header_t;

#define	MCAST_PACKET_SIZE 1500

#define	PACKET_HEADER_LENGTH ( sizeof(packet_header_t) )
#define	CHUNK_SIZE ( MCAST_PACKET_SIZE - PACKET_HEADER_LENGTH )
#define	PACKET_PAYLOAD_SIZE ( CHUNK_SIZE + PACKET_HEADER_LENGTH )

packet_header_t	packet_construct_header(uint8_t flag);

packet_header_t *packet_get_hdr(const void *data);

int	packet_get_data( packet_header_t *h, const void *data, uint8_t  *plane);

int	packet_put_data( packet_header_t *h, void *payload, const uint8_t *plane );

int	packet_put_padded_data( packet_header_t *h, void *payload, const uint8_t *plane, int bytes );

#endif
