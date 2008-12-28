/*
Copyright © 1998. The Regents of the University of California (Regents). 
All Rights Reserved.

Written by Matt Wright, The Center for New Music and Audio Technologies,
University of California, Berkeley.

Permission to use, copy, modify, distribute, and distribute modified versions
of this software and its documentation without fee and without a signed
licensing agreement, is hereby granted, provided that the above copyright
notice, this paragraph and the following two paragraphs appear in all copies,
modifications, and distributions.

IN NO EVENT SHALL REGENTS BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT,
SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING
OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF REGENTS HAS
BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

REGENTS SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
PURPOSE. THE SOFTWARE AND ACCOMPANYING DOCUMENTATION, IF ANY, PROVIDED
HEREUNDER IS PROVIDED "AS IS". REGENTS HAS NO OBLIGATION TO PROVIDE
MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

The OpenSound Control WWW page is 
    http://www.cnmat.berkeley.edu/OpenSoundControl
*/

/*
  NetworkReturnAddress.c

  This version implements UDP return addresses on SGI

  Matt Wright, 
  9/11/98
*/

#include <libOSC/OSC-common.h>
#include <libOSC/OSC-timetag.h>
#include <libOSC/OSC-address-space.h>
#include <libOSC/NetworkReturnAddress.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <libOSC/NetworkUDP.h>



int SizeOfNetworkReturnAddress(void) {
    return sizeof(struct NetworkReturnAddressStruct);
}

Boolean NetworkSendReturnMessage(NetworkReturnAddressPtr addr,
				 int n,
				 void *buf) {
    if (addr == 0) return FALSE;

    return n == sendto(addr->sockfd, buf, n, 0,(const struct sockaddr*) &(addr->cl_addr), addr->clilen);
}
