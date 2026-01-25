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

typedef struct
{
	unsigned char *area;
	unsigned char **mag;
	unsigned int rounds;
	void	*next;
} space_t;

typedef struct
{
	space_t **spaces;	/*!<  array of spaces */
	space_t *space;		/*!<  single space */
} pool_t;

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

void	*vevo_pool_alloc( void *p, size_t bs, unsigned int k )
{
	pool_t *pool = (pool_t*) p;
	space_t *space = pool->spaces[k];
	if( !space || space->rounds == 0 )
	{
		space_t *m = alloc_space( bs );
		m->next = space;
		pool->spaces[k] = m;
		space = m;
	}
    unsigned char *block = space->mag[--space->rounds];
    return (void*) block;
}

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

void vevo_pool_slice_destroy( void *p )
{
    if (!p) return;

    pool_t *pool = (pool_t*) p;
    space_t *s = pool->space;
    space_t *n = NULL;

    while( s != NULL )
    {
        n = s;
        s = n->next; 
        
        if(n->area) free( n->area );
        if(n->mag)  free( n->mag );
        free( n );
    }   
    
    free( pool );
}

void	*vevo_pool_slice_alloc( void *p, size_t bs )
{
	pool_t *pool = (pool_t*) p;
	space_t *space = pool->space;
	if( space->rounds == 0 )
	{ 
		space_t *m = alloc_space( bs );
		m->next = space;
		pool->space = m;
		space = m;
	}
	void **mag =(void**) pool->space->mag;
	return mag[ --space->rounds ];
}

void vevo_pool_slice_free( void *p, void *ptr )
{
    pool_t *pool = (pool_t*) p;
    space_t *space = pool->space;
    
    if( space->rounds < ROUNDS_PER_MAG )
    {
        void **mag = (void**)space->mag;
        mag[ space->rounds++ ] = ptr;
        return;
    }

    space_t *l = space->next; // start at next, we already checked head
    while( l != NULL )
    {
        if( l->rounds < ROUNDS_PER_MAG )
        {
            void **mag = (void**)l->mag;
            mag[ l->rounds ++ ] = ptr;
            return;
        }
        l = l->next;
    }

#ifdef STRICT_CHECKING 
     assert(0 && "Pool Overflow: Double free detected or invalid pointer")
#endif
}
