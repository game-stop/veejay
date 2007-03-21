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
  OSC-callbacklist.h
  Linked lists of methods

  Matt Wright, 3/13/98

  include "OSC-dispatch.h" before this file.

*/


/* Call this before you call anything else. */
Boolean InitCallbackListNodes(int numNodes, void *(*InitTimeMalloc)(int numBytes));

callbackList AllocCallbackListNode(methodCallback callback, void *context, 
				   struct callbackListNode *next);

void FreeCallbackListNode(callbackList);


