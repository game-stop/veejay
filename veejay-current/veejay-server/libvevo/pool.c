/*
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

//! \typedef space_t structure
/*! The space_t structure is a linked list of spaces.
   Each space has a magazine that can hold up to ROUNDS_PER_MAG rounds.
   The magazine is a stack. 
   Each round in the magazine is of a fixed size
 */
typedef struct
{
	unsigned char *area;		/*!< Pointer to memory space containing ROUNDS_PER_MAG objects */
	unsigned char **mag;		/*!< A magazine is a ROUNDS_PER_MAG-element array of pointers to objects */
	unsigned int	rounds;		/*!< Available rounds*/
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
	unsigned char *p;
	space_t *s;
	s = (space_t*) vj_malloc(sizeof(space_t));
	s->area = vj_calloc(bs * (ROUNDS_PER_MAG+1));
	s->mag  = vj_calloc( sizeof(void*) * (ROUNDS_PER_MAG + 2) );
	
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

int	vevo_pool_size( void *p )
{
	return 0;
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
	void **mag = (void**)pool->spaces[k]->mag;
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
	unsigned char **mag = space->mag;
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
	mag[  space->rounds++ ] = ptr;
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
	for( i = 0 ; i < Mend ; i ++ )
	{
		if( nS[i] == NULL )
			continue;
		space_t *n = nS[i];
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
	if( pool->space )
		free(pool->space);
	
	free( pool );
	pool = NULL;
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
	//free( pool->space );
	free( pool );
	p = NULL;
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
	void **mag =(void**) pool->space->mag;
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
	void **mag = (void**)space->mag;
	if( n == ROUNDS_PER_MAG )
	{ 
		space_t *l = space;
		while( l != NULL )
		{
			if( l->rounds < ROUNDS_PER_MAG )
			{
				mag = (void**)l->mag;
				mag[ l->rounds ++ ] = ptr;
				return;
			}
			l = l->next;
		}
	}
	mag[ space->rounds++ ] = ptr;
}
