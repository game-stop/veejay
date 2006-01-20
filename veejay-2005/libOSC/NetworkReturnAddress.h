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
  NetworkReturnAddress.h

  API that the OSC Kit uses to deal with network return addresses.  You will
  fill in parts of this file and write NetworkReturnAddress.c to implement
  this API via whatever network services you use.

  NB:  This API is the only interface the Kit uses for dealing with network
  addresses, but of course the part of the application that accepts incoming
  packets needs to know about network return addresses so it can fill in the
  correct return address when it receives a packet.

  Matt Wright, 
  6/3/98
*/

/* Return sizeof(struct NetworkReturnAddressStruct). */
int SizeOfNetworkReturnAddress(void);

/* Send a packet back to the client, or do nothing if addr==0 */
Boolean NetworkSendReturnMessage(NetworkReturnAddressPtr addr,
				 int n,
				 void *buf);
