/* vjnet - low level network I/O for VeeJay
 *
 *           (C) 2005-2007 Niels Elburg <nwelburg@gmail.com> 
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
	uint8_t		flag;
	uint8_t		seq_num;
	uint32_t	usec;
	uint32_t	timeout;	
	uint8_t		length;
} packet_header_t;

typedef struct
{
	uint16_t	width;
	uint16_t	height;
	uint8_t		fmt;
	uint32_t	len;
	uint8_t		mode;
} frame_info_t;


#define			PACKET_HEADER_LENGTH	( sizeof(packet_header_t) )
#define			PACKET_APP_HEADER_LENGTH ( sizeof(frame_info_t) )
#define			CHUNK_SIZE		( 1500 - 32 )
#define			PACKET_PAYLOAD_SIZE	(CHUNK_SIZE + PACKET_HEADER_LENGTH + PACKET_APP_HEADER_LENGTH )

void			packet_dump_header( packet_header_t *h);

void			packet_dump_info( frame_info_t * i );

packet_header_t		packet_construct_header(uint8_t flag);

packet_header_t		packet_get_header(const void *data);

int			packet_get_data( packet_header_t *h, const void *data, uint8_t  *plane);

int			packet_put_data( packet_header_t *h, frame_info_t *i, void *payload, const uint8_t *plane );

int			packet_put_padded_data( packet_header_t *h, frame_info_t *i, void *payload, const uint8_t *plane, int bytes );

int			packet_get_info(frame_info_t *i, const void *data );
#endif
