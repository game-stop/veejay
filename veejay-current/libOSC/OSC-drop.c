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


/* OSC-drop.c
   
   This implementation just prints a warning.

   Matt Wright, 3/16/98
*/
#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <libOSC/OSC-common.h>
#include <libOSC/OSC-timetag.h>
#include <libOSC/OSC-address-space.h>
#include <libOSC/NetworkReturnAddress.h>
#include <libOSC/OSC-receive.h>
#include <libOSC/OSC-drop.h>

void DropPacket(OSCPacketBuffer p) {
    OSCWarning("Packet dropped.");
}
    
void DropBundle(char *buf, int n, OSCPacketBuffer p) {
    OSCWarning("Bundle dropped.");
}

void DropMessage(char *buf, int n, OSCPacketBuffer p) {
    OSCWarning("Message dropped.");
}
    
