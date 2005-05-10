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
#ifndef NETCOMMON_H
#define NETCOMMON_H

#define	VSOCK_S	0
#define VSOCK_C 1
#define V_STATUS 0
#define V_CMD 1
#define VMCAST_S 4
#define VMCAST_C 5

enum
{
	VJ_CMD_PORT=0,
	VJ_STA_PORT=1,
	VJ_CMD_MCAST=3,
	VJ_CMD_MCAST_IN=4,
	VJ_CMD_OSC=2,
};

#endif

