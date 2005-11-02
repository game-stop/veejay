/* VeVo is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   VeVo is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this source code; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

# ifndef LIVIDO_ATOM_H_INCLUDED
# define LIVIDO_ATOM_H_INCLUDED

#include <stdio.h>
#include <stdint.h>
// using kazlib
#include "hash.h"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

/* Mediation layer, provided by this host */
typedef struct {
    void *value;
    size_t size;
} atom_t;

typedef struct {
    int atom_type;
    union {
	atom_t *atom;
	atom_t **array;
    } elements;
    int num_elements;
    int flags;
} livido_storage_t;

#define HAVE_LIVIDO_PORT_T
typedef struct {
    hash_t *table;
} livido_port_t;
# endif
