/*Copyright (c) 2004-2005 N.Elburg <nwelburg@gmail.com>

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
#ifndef POOLM
#include <config.h>
#define POOLM
#define	ROUNDS_PER_MAG	16	

#define M4b	0
#define	M8b	1
#define Mpb	2
#define Mstor   3
#define Matom   4
#define Midx    5
#define Mprop	6
#define M64b	7
#define Mend    8

#ifdef STRICT_CHECKING
int	vevo_pool_size(void *pool);
#endif

void	*vevo_pool_alloc( void *pool, size_t bs, unsigned int k );
void	vevo_pool_free( void *pool, void *ptr, unsigned int k );
void	*vevo_pool_init(size_t property_size, size_t stor_size, size_t atom_size, size_t index_size);
void	vevo_pool_destroy( void *p );
void	vevo_pool_slice_destroy( void *p );
//void	*vevo_pool_slice_init( size_t node_size );
void	*vevo_pool_slice_alloc( void *pool, size_t bs);
void	vevo_pool_slice_free( void *pool, void *ptr );

#define vevo_pool_alloc_property(type,pool) vevo_pool_alloc(pool,sizeof(type),Mprop )
#define	vevo_pool_alloc_storage(type,pool) vevo_pool_alloc( pool, sizeof(type),Mstor )
#define	vevo_pool_alloc_atom(type,pool) vevo_pool_alloc( pool, sizeof(type), Matom )
#define vevo_pool_alloc_node(type,pool) vevo_pool_alloc( pool, sizeof(type), Midx )
#define vevo_pool_alloc_int(type,pool) vevo_pool_alloc( pool,sizeof(type), M4b )
#define vevo_pool_alloc_ptr(type,pool) vevo_pool_alloc( pool,sizeof(type), Mpb )
#define vevo_pool_alloc_dbl(type,pool) vevo_pool_alloc( pool,sizeof(type), M8b )
#define vevo_pool_alloc_64b(type,pool) vevo_pool_alloc( pool,sizeof(type), M64b )

#define vevo_pool_free_property(pool,ptr) vevo_pool_free(pool,ptr,Mprop )
#define vevo_pool_free_storage( pool,ptr ) vevo_pool_free( pool, ptr, Mstor )
#define vevo_pool_free_atom( pool,ptr ) vevo_pool_free( pool, ptr, Matom )
#define vevo_pool_free_node( pool,ptr ) vevo_pool_free( pool, ptr, Midx )
#define vevo_pool_free_int( pool,ptr ) vevo_pool_free( pool,ptr, M4b )
#define vevo_pool_free_ptr( pool,ptr ) vevo_pool_free( pool,ptr, Mpb )
#define vevo_pool_free_dbl( pool,ptr ) vevo_pool_free( pool,ptr,M8b )
#define vevo_pool_free_64b( pool,ptr ) vevo_pool_free( pool,ptr,M64b )


#endif
