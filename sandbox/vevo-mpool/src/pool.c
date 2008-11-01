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
#ifdef VEVO_MEMPOOL
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <sys/mman.h>

#include <include/lowlevel.h>
#include <include/pool.h>
#define FREE_LISTS 5

typedef struct {
    int vevo_type;
    size_t size;
    void *ptr;
    void *next;
} free_node_t;

typedef free_node_t free_list_t;
typedef struct {
    size_t l[2];
    void *a4;
    void *p4;
    void *t;
    double *d;
    void *s;
    free_list_t **f;
    int idx[2];
} pool_t;

static struct {
    int index;
    size_t size;
} type_list[] = {
    {
    ATOM_INT, sizeof(int32_t)}
    , {
    ATOM_PTR, sizeof(void *)}, {
    ATOM_DBL, sizeof(double)}, {
    ATOM_ATOM, sizeof(atom_t)}
,};

//clean up todo!
static free_node_t *vevo_new_node(int vevo_type, void *area)
{
    free_node_t *n = (free_node_t *) malloc(sizeof(free_node_t));
    n->size = type_list[vevo_type].size;
    n->ptr = area;
    n->next = NULL;
    return n;
}

static void vevo_node_free(free_node_t * n)
{
    if (n)
	free(n);
    n = NULL;
}

static void append_node_to_freelist(pool_t * p, int vevo_type,
				    size_t offset, void *base)
{
    free_node_t *n = vevo_new_node(vevo_type, base);
    free_node_t *c = NULL;
    free_list_t *l = p->f[vevo_type];
    if (l == NULL)
	p->f[vevo_type] = n;
    else {
	while (l != NULL) {
	    c = (free_node_t *) l->next;
	    if (c == NULL) {
		l->next = (void *) n;
		break;
	    }
	    l = c;
	}
    }
}

void *vevo_new_pool(void)
{
    pool_t *p = (pool_t *) malloc(sizeof(pool_t));
    memset(p, 0, sizeof(pool_t));
    p->f = (free_list_t **) malloc(sizeof(free_list_t *) * FREE_LISTS);
    memset(p->f, 0, sizeof(free_list_t *) * FREE_LISTS);

    p->l[0] = (VEVO_MEM_LIMIT / 4) * 3;
    p->l[1] = (VEVO_MEM_LIMIT / 4) * 1;

    size_t chunk = (p->l[0] / 2) * sizeof(int32_t);
    size_t d_chunk = p->l[1] * sizeof(double);
    size_t t_chunk = VEVO_MEM_LIMIT * sizeof(atom_t);

    p->a4 = (void *) malloc(p->l[0] * sizeof(int32_t));	// int and bool
    p->p4 = (void *) malloc(p->l[0] * sizeof(void *));	// port and ptr
    p->d = (double *) malloc(p->l[1] * sizeof(double));	// double
    p->t = (atom_t *) malloc(VEVO_MEM_LIMIT * sizeof(atom_t));

    memset(p->a4, 0, p->l[0] * sizeof(int32_t));
    memset(p->p4, 0, p->l[0] * sizeof(void *));
    memset(p->d, 0, p->l[1] * sizeof(double));
    memset(p->t, 0, VEVO_MEM_LIMIT * sizeof(atom_t));

    void *base = NULL;

    int n = 0;
    /* initialize free lists for int and bool types */
    size_t it = 0;
    while (it < chunk) {
	base = ((int32_t *) p->a4 + it);
	append_node_to_freelist(p, ATOM_INT, chunk, base);
	it += type_list[ATOM_INT].size;
	n++;
    }

    /* initalize ptr free lists */
    it = 0;
    while (it < chunk) {
	base = ((void **) p->p4 + (ATOM_PTR * chunk + it));
	append_node_to_freelist(p, ATOM_PTR, chunk, base);
	it += type_list[ATOM_PTR].size;
	n++;
    }

    /* initialize double free lists */
    it = 0;
    while (it < d_chunk) {
	base = (void *) ((double *) p->d + it);
	append_node_to_freelist(p, ATOM_DBL, d_chunk, base);
	it += type_list[ATOM_DBL].size;
	n++;
    }
    /* fill to hold atom container */
    it = 0;
    while (it < t_chunk) {
	base = (void *) ((double *) p->t + it);
	append_node_to_freelist(p, ATOM_ATOM, t_chunk, base);
	it += type_list[ATOM_ATOM].size;
    }

    p->idx[0] = n;
    p->idx[1] = 0;
#ifdef STRICT_CHECKING
    assert(p->idx[0] != 0);
#endif

    return (void *) p;
}

void vevo_free_pool(void *v)
{
    int n = 0;
    pool_t *p = (pool_t *) v;
    if (p) {
	if (p->a4)
	    free(p->a4);
	if (p->p4)
	    free(p->p4);
	if (p->d)
	    free(p->d);
	if (p->t)
	    free(p->t);
	for (n = 0; n < FREE_LISTS; n++) {
	    free_node_t *node = NULL;
	    while (p->f[n] != NULL) {
		node = (free_node_t *) p->f[n]->next;
		vevo_node_free(p->f[n]);
		p->f[n] = node;
	    }
	}
	free(p->f);
	free(p);
    }
}

void *vevo_malloc(void *v, int vevo_type)
{
    pool_t *p = (pool_t *) v;
    free_node_t *l = p->f[vevo_type];
#ifdef STRICT_CHECKING
    assert(l != NULL);
#endif

    while (l != NULL) {
	if (l->size == 0)
	    l = l->next;
	else {			/* found memory */
	    void *res = l->ptr;
	    free_node_t *next = l->next;
	    l->size = 0;
	    l->vevo_type = vevo_type;
	    l = next;
	    p->idx[1]++;
#ifdef STRICT_CHECKING
	    assert(p->idx[1] < p->idx[0]);
#endif
	    return res;
	}
    }
#ifdef STRICT_CHECKING
    assert(0);
#endif
    return NULL;
}

int vevo_pool_verify(void *v)
{
    pool_t *p = (pool_t *) v;
    return (p->idx[0] > p->idx[1] ? 1 : 0);
}


void vevo_free(void *v, void *ptr, int vevo_type)
{
    pool_t *p = (pool_t *) v;
    free_node_t *l = NULL;
    l = p->f[vevo_type];
    while (l != NULL) {
	if (l->ptr == ptr) {
	    l->size = type_list[vevo_type].size;
	    p->idx[1]--;
#ifdef STRICT_CHECKING
	    assert(p->idx[1] >= 0);
#endif
	    return;
	}
	l = l->next;
    }
#ifdef STRICT_CHECKING
    assert(0);
#endif
}
#endif
