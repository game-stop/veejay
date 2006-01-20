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

#ifndef PACKET_H
#define PACKET_H
/*

Veejay - Video streaming over UDP (Multicast)

Veejay doesnt care about dropped packet. Dropped packet means drop frame.

video packet header is 16 bytes in length:

0------8-----......-16---....----48--...------96-- .... 128
flag   |  seq. num   | time sec   | time msec  | timeout  |
-----------------------------------------------------------

flag:
	if this flag is set, the fixed header is followed by 1 header extension,
        that describes the video format for the application

seq.num:
	the sequence number increments by one for each data packet sent, it
        is used by the receiver to restore the video frame. the initial value is 0.

time sec:
time usec:	
	the timestamp ; the time set from a clock that increments monotonically and
        linear in time (to allow synchronization and detection of jittering).
        currently, the absolute value of the system clock is used but this will
	be changed in later revisions.

timeout:
	the recommended timeout (set by sender) to use the smallest timeout possible
        on the receiver end. 


video format header is 5 bytes in length
0----...16 -----.... 32 ---- 48
| width |   height    | format|
-------------------------------

width:
	the width of the video frame

height:
	the height of the video frame

format:
	the format of the video frame
	veejay works nativly with YUV 4:2:0 / 4:2:2 Planar (First come all Y, then all Cb, then all Cr) 
        therefore 0 = 420 , 1 = 422 (for now)


Note that this code is very naive ; it does not do byteswapping for
little/big endian machines. Also this is on the TODO list!
Meaning thus veejay x86 cannot communicate with veejay ppc !!!
	
*/
#include	<stdio.h>
#include	<sys/time.h>
#include 	<stdint.h>

typedef struct
{
	uint8_t		flag;
	uint16_t	seq_num;
	uint32_t	sec;
	uint32_t	usec;
	uint32_t	timeout;	
} packet_header_t;

typedef struct
{
	uint16_t	width;
	uint16_t	height;
	uint8_t		fmt;
} frame_info_t;

#define			CHUNK_SIZE	16384

#define			PACKET_HEADER_LENGTH	( sizeof(packet_header_t) )
#define			PACKET_APP_HEADER_LENGTH ( sizeof(frame_info_t) )

#define			PACKET_PAYLOAD_SIZE	(CHUNK_SIZE + PACKET_HEADER_LENGTH + PACKET_APP_HEADER_LENGTH )

void			packet_dump_header( packet_header_t *h);

packet_header_t		packet_construct_header(uint8_t flag);

packet_header_t		packet_get_header(const void *data);

int			packet_get_data( packet_header_t *h, frame_info_t *i, const void *data, uint8_t  *plane);

int			packet_put_data( packet_header_t *h, frame_info_t *i, void *payload, const uint8_t *plane );

#endif
