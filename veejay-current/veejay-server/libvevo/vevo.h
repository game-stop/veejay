/*
Copyright (c) 2004-2005 N.Elburg <nwelburg@gmail.com>

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
#define VEVO_FF_PORT 10                 // free frame port
#define VEVO_FF_PARAM_PORT 11           // free frame parameter port

#define VEVO_FR_PORT    20              // frei0r port
#define VEVO_FR_PARAM_PORT 21           // frei0r parameter port

#define VEVO_LIVIDO_PORT        30      // livido port
#define VEVO_LIVIDO_PARAM_PORT  31      // livido parameter port
#define VEVO_ILLEGAL 100

#define VEVO_EVENT_PORT         321

#define VEVO_VJE_PORT           32


#define VEVO_CACHE_PORT         40      // linked list 
#define VEVO_PORT_REFERENCES    1040    // hash
#define VEVO_SAMPLE_PORT        2035    // sample
#define VEVO_SAMPLE_BANK_PORT   2036    // bank

#define VEVO_VJE_INSTANCE_PORT  33

#define HAVE_LIVIDO_PORT_T
typedef void livido_port_t;
# endif


#define	LIVIDO_ATOM_TYPE_INT	VEVO_ATOM_TYPE_INT
#define LIVIDO_ATOM_TYPE_DOUBLE	VEVO_ATOM_TYPE_DOUBLE
#define LIVIDO_ATOM_TYPE_BOOLEAN VEVO_ATOM_TYPE_BOOL
#define LIVIDO_ATOM_TYPE_STRING  VEVO_ATOM_TYPE_UTF8STRING
#define LIVIDO_ATOM_TYPE_VOIDPTR VEVO_ATOM_TYPE_VOIDPTR
#define LIVIDO_ATOM_TYPE_PORTPTR VEVO_ATOM_TYPE_PORTPTR

