/*
 * veejay - Linux VeeJay
 * 	     (C) 2003 Niels Elburg <elburg@hio.hen.nl> <nelburg@looze.net>
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

#ifndef VJ_OSC 
#define VJ_OSC

#include <libOSC/libosc.h>
#include "vj-lib.h"
#include <sys/types.h>
typedef struct osc_arg_t {
    int a;
    int b;
    int c;
} osc_arg;

typedef struct vj_osc_t {
  struct OSCAddressSpaceMemoryTuner t;
  struct OSCReceiveMemoryTuner rt;
  struct OSCContainerQueryResponseInfoStruct cqinfo;
  struct OSCMethodQueryResponseInfoStruct ris;
  struct sockaddr_in cl_addr;
  int sockfd;
  int clilen;
  fd_set readfds;
  OSCcontainer container;
  OSCcontainer *leaves;
  OSCPacketBuffer packet;
  osc_arg *osc_args;
} vj_osc;

int vj_osc_setup_addr_space(vj_osc *o);
int vj_osc_get_packet(vj_osc *o);
void vj_osc_free(vj_osc *o);
void vj_osc_dump();
vj_osc* vj_osc_allocate(int port_id);
 
#endif
