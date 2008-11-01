/*
Copyright (c) 2004-2005 N.Elburg <nelburg@looze.net>

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

# ifndef VEVO_H_INCLUDED
# define VEVO_H_INCLUDED

#include <stdio.h>
#include <stdint.h>

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define HAVE_VEVO_PORT_T
typedef void livido_port_t;


///# sed s/LIVIDO/VEVO
#define VEVO_PROPERTY_READONLY (1<<0)

#define VEVO_NO_ERROR 0
#define VEVO_ERROR_MEMORY_ALLOCATION 1
#define VEVO_ERROR_PROPERTY_READONLY 2
#define VEVO_ERROR_NOSUCH_ELEMENT 3
#define VEVO_ERROR_NOSUCH_PROPERTY 4
#define VEVO_ERROR_WRONG_ATOM_TYPE 5
#define VEVO_ERROR_TOO_MANY_INSTANCES 6
#define VEVO_ERROR_HARDWARE 7
#define VEVO_ATOM_TYPE_INT 1
#define VEVO_ATOM_TYPE_DOUBLE 2
#define VEVO_ATOM_TYPE_BOOLEAN 3
#define VEVO_ATOM_TYPE_STRING 4
#define VEVO_ATOM_TYPE_VOIDPTR 65
#define VEVO_ATOM_TYPE_PORTPTR 66


# endif



