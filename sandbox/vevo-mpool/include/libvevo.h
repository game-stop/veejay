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
#ifndef __LIBVEVO_H__
#define __LIBVEVO_H__

#ifdef __cplusplus
extern "C" {
#endif				/* __cplusplus */

#include <sys/types.h>

#ifndef HAVE_LIVIDO_PORT_T
#define HAVE_LIVIDO_PORT_T
    typedef void livido_port_t;
#endif

#include <include/livido.h>

    extern void vevo_port_free(livido_port_t * port);
    extern livido_port_t *vevo_port_new(int port_type);
    extern int vevo_property_set(livido_port_t * port, const char *key,
				 int atom_type, int num_elems,
				 void *value);
    extern int vevo_property_get(livido_port_t * port, const char *key,
				 int idx, void *value);
    extern int vevo_property_num_elements(livido_port_t * port,
					  const char *key);

    extern int vevo_property_atom_type(livido_port_t * port,
				       const char *key);

    extern size_t vevo_property_element_size(livido_port_t * port,
					     const char *key,
					     const int idx);
    extern char **vevo_list_properties(livido_port_t * port);


#ifdef __cplusplus
}
#endif				/* __cplusplus */
#endif				// #ifndef __LIBVEVO_H__
