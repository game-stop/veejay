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

 /** \defgroup mem_pool Efficient Object Caching
 *
 * 	To reduce the overhead of malloc/free when allocating and freeing many small objects
 *	I keep a linked list of Spaces. Each Space holds a continuous
 *	memory area that is of size ROUNDS_PER_MAG * sizeof(type). This area is divided
 *	into ROUND_PER_MAG chunks. The malloc() replacement pops a round from the stack,
 *	whilst the free() replacement pushes a round back to the stack.
 *	The stack size is limited to ROUNDS_PER_MAG
 *	When the stack is full, a new magazine is allocated and added to the linked list
 *	of magazines.	
 *
 *	This is basically how the slab allocator works in a linux kernel
*/
#include <config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <libvjmem/vjmem.h>
#include "pool.h"

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

//! \typedef space_t structure
/*! The space_t structure is a linked list of spaces.
   Each space has a magazine that can hold up to ROUNDS_PER_MAG rounds.
   The magazine is a stack. 
   Each round in the magazine is of a fixed size
 */
typedef struct
{
	void *area;		/*!< Pointer to memory space containing ROUNDS_PER_MAG objects */
	void **mag;		/*!< A magazine is a ROUNDS_PER_MAG-element array of pointers to objects */
	int	rounds;		/*!< Available rounds*/
	void	*next;		/*!< Pointer to next space */
} space_t;

//! \typedef pool_t structure
/*! The pool_t structure is a pool of spaces
 *  Each pool has 1 or more spaces.
 */
typedef struct
{
	space_t **spaces;	/*!<  array of spaces */
	space_t *space;		/*!<  single space */
} pool_t;

//!Allocate a new space of a fixed size
/*!
 \param bs size in bytes
 \return New space that holds ROUNDS_PER_MAG rounds
 */
static space_t	*alloc_space( size_t bs )
{
	int k;
	void *p;
	space_t *s;
	s = (space_t*) vj_malloc(sizeof(space_t));
#ifdef STRICT_CHECKING
	assert( s != NULL );
#endif
	s->area = vj_malloc(bs * ROUNDS_PER_MAG);
	s->mag  = vj_malloc( sizeof(void*) * (ROUNDS_PER_MAG + 1) );
	p = s->area;
	for( k = 0; k <= ROUNDS_PER_MAG  ;k ++ )
	{
		s->mag[k] = p;
		p += bs;	
	}
	s->rounds = ROUNDS_PER_MAG;
	s->next = NULL;
	return s;
}

//! Allocate a new pool with spaces of various fixed sizes
/*!
 \param prop_size size in bytes of vevo_property_t
 \param stor_size size in bytes of vevo_storage_t
 \param atom_size size in bytes of atom_t
 \param index_size size in bytes of prop_node_t
 \return A new pool that holds various fixed sized Spaces
 */
void	*vevo_pool_init(size_t prop_size,size_t stor_size, size_t atom_size, size_t index_size)
{
	unsigned int Msize = Mend + 1;
	pool_t *p = (pool_t*) vj_malloc(sizeof(pool_t));
#ifdef STRICT_CHECKING
	assert( p != NULL );
#endif
	p->space = NULL;
	p->spaces = (space_t**) vj_malloc(sizeof(space_t*) * Msize );
	p->spaces[M4b] = alloc_space( sizeof(int32_t) );
	p->spaces[M8b] = alloc_space( sizeof(double) );
	p->spaces[Mpb] = alloc_space( sizeof(void*) );
	p->spaces[M64b] = alloc_space(sizeof(uint64_t) );
	p->spaces[Mprop] = alloc_space( prop_size );
	p->spaces[Mstor] = alloc_space( stor_size );
	p->spaces[Matom] = alloc_space( atom_size );
	p->spaces[Midx]  = alloc_space( index_size );
	p->spaces[Mend] = NULL;
	return (void*)p;
}

//! Allocate a new pool with a single space of a fixed size
/*!
 \param node_size size in bytes of a single block
 \return A new pool with a single space
 */
void	*vevo_pool_slice_init( size_t node_size )
{
	pool_t *p = (pool_t*) malloc(sizeof(pool_t));
	p->spaces = NULL;
	p->space = alloc_space( node_size );
	return p;	
}

//! Get a pointer to the starting address of an unused block. Pops a round from the magazine and creates a new space if magazine is empty. 
/*!
 \param p pointer to pool_t structure
 \param bs size of block to allocate
 \param k base type of block to allocate
 \return pointer to free block
 */
void	*vevo_pool_alloc( void *p, size_t bs, unsigned int k )
{
	pool_t *pool = (pool_t*) p;
	space_t *space = pool->spaces[k];
	if( space->rounds == 0 )
	{ // no more rounds to fire, create a new magazine and add it to the list
		space_t *m = alloc_space( bs );
		m->next = space;
		pool->spaces[k] = m;
		space = m;
	}
	void **mag = pool->spaces[k]->mag;
	return mag[ --space->rounds ];
}

//! Pushes a round to a magazine that is not full
/*!
 \param p pointer to pool_t structure
 \param ptr pointer to address of block
 \param k base type of block to allocate
 */
void	vevo_pool_free( void *p, void *ptr, unsigned int k )
{
	pool_t *pool = (pool_t*) p;
	unsigned int n = pool->spaces[k]->rounds;
	space_t *space = pool->spaces[k];
	void **mag = space->mag;
	if( n == ROUNDS_PER_MAG )
	{ 
		space_t *l = space;
		while( l != NULL )
		{
			if( l->rounds < ROUNDS_PER_MAG )
			{
				mag = l->mag;
				mag[ l->rounds ++ ] = ptr;
				return;
			}
			l = l->next;
		}
	}
	mag[ space->rounds++ ] = ptr;
}

//! Destroy a pool and all spaces. Frees all used memory
/*!
 \param p pointer to pool_t structure
 */
void	vevo_pool_destroy( void *p )
{
	pool_t *pool = (pool_t*) p;
	space_t **nS = pool->spaces;
	int i ;
	for( i = 0 ; nS[i] != NULL ; i ++ )
	{
		space_t *n = pool->spaces[i];
		space_t *k = NULL;
		while( n != NULL )
		{
			k = n;
			free( k->area );
			free( k->mag );
			n = k->next;
			free( k );
		}
	}
	free( nS );
	free( pool );
}

//! Destroy a pool and the space it holds. Frees all used memory
/*!
 \param p pointer to pool_t structure
 */
void	vevo_pool_slice_destroy( void *p )
{
	pool_t *pool = (pool_t*) p;
	space_t *s = pool->space;
	space_t *n = NULL;
	while( s != NULL )
	{
		n = s;
		free( n->area );
		free( n->mag );
		s = n->next;
		free( n );
	}	
	free( pool->space );
	free( pool );
}

//! Get a pointer to the starting address of an unused block. Pops a round from the magazine and creates a new space if magazine is empty. 
/*!
 \param p pointer to pool_t structure
 \param bs size of block to allocate
 \return pointer to free block
 */
void	*vevo_pool_slice_alloc( void *p, size_t bs )
{
	pool_t *pool = (pool_t*) p;
	space_t *space = pool->space;
	if( space->rounds == 0 )
	{ // no more rounds to fire, create a new magazine and add it to the list
		space_t *m = alloc_space( bs );
		m->next = space;
		pool->space = m;
		space = m;
	}
	void **mag = pool->space->mag;
	return mag[ --space->rounds ];
}

//! Pushes a round to a magazine that is not full
/*!
 \param p pointer to pool_t structure
 \param ptr pointer to address of block
 */
void	vevo_pool_slice_free( void *p, void *ptr )
{
	pool_t *pool = (pool_t*) p;
	unsigned int n = pool->space->rounds;
	space_t *space = pool->space;
	void **mag = space->mag;
	if( n == ROUNDS_PER_MAG )
	{ 
		space_t *l = space;
		while( l != NULL )
		{
			if( l->rounds < ROUNDS_PER_MAG )
			{
				mag = l->mag;
				mag[ l->rounds ++ ] = ptr;
				return;
			}
			l = l->next;
		}
	}
	mag[ space->rounds++ ] = ptr;
}
