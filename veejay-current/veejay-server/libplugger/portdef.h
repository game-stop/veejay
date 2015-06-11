/* veejay - Linux VeeJay
 *           (C) 2002-2004 Niels Elburg <nwelburg@gmail.com> 
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


#ifndef PORTDEF_H
#define PORTDEF_H
#define VEVO_FF_PORT 10                 // free frame port
#define VEVO_FF_PARAM_PORT 11           // free frame parameter port

#define VEVO_FR_PORT    20              // frei0r port
#define VEVO_FR_PARAM_PORT 21           // frei0r parameter port

#define VEVO_LIVIDO_PORT        30      // livido port
#define VEVO_LIVIDO_PARAM_PORT  31      // livido parameter port
#define VEVO_ILLEGAL 100

#define	VEVO_EVENT_PORT		321

#define	VEVO_VJE_PORT		32


#define	VEVO_CACHE_PORT		40	// linked list 
#define VEVO_PORT_REFERENCES	1040	// hash
#define	VEVO_SAMPLE_PORT	2035	// sample
#define VEVO_SAMPLE_BANK_PORT	2036	// bank

#define	VEVO_VJE_INSTANCE_PORT	33

/*
#define LIVIDO_PORT_TYPE_PLUGIN_INFO 1
#define LIVIDO_PORT_TYPE_FILTER_CLASS 2
#define LIVIDO_PORT_TYPE_FILTER_INSTANCE 3
#define LIVIDO_PORT_TYPE_CHANNEL_TEMPLATE 4
#define LIVIDO_PORT_TYPE_PARAMETER_TEMPLATE 5
#define LIVIDO_PORT_TYPE_CHANNEL 6
#define LIVIDO_PORT_TYPE_PARAMETER 7
#define LIVIDO_PORT_TYPE_GUI 8
*/
#endif
