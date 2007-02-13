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

/** \defgroup VeVo Veejay Video Objects
 *
 * VeVo provides a data model for handling dynamic
 * objects in runtime.
 *  
 * Features:
 *       -# Efficient object caching with reference counting
 *       -# Generic per object properties with set/get functions
 *       -# Fast hashing of keys
 *
 * To distinguish between objects as in object oriented programming,
 * Ports are introduced. A Port is a container of properties.
 * 
 * A property is a tupple of a value and an unique key. 
 * A key is a human friendly name for a Property. Internally a list
 * of mnenomics is kept to translate the human friendly name into a numeric value  
 * that uniquely identifies an object.   
 *
 * Types of values:
 *        -# int32_t VEVO_ATOM_TYPE_INT
 *        -# char*   VEVO_ATOM_TYPE_STRING
 *        -# void*   VEVO_ATOM_TYPE_VOIDPTR
 *        -# void*   VEVO_ATOM_TYPE_PORTPTR
 *        -# int32_t VEVO_ATOM_TYPE_BOOLEAN
 *	  -# uint64_t VEVO_ATOM_TYPE_UINT64
 *
 *       
 *
 * Values of type STRING must always be NULL-terminated, vevo creates
 * a dynamic copy on both set and get functions.
 *
 * Values of type VOIDPTR must always be freed by caller.
 *
 * Values of type PORTPTR will be cleaned up by vevo when no longer needed.
 *
 * Depending on the type of port that is constructed
 *        -# the port uses a linked list of objects
 *        -# the port uses a hash table of objects
 *        -# the port's type is stored as "type"       
 *        -# the port's type is not stored (anonymous port)
 * 
 * A Port has no limit on the amounts of properties it can hold. 
 * Vevo can recursivly walk all ports and free them, except for
 * those Properties that are of the VEVO_ATOM_TYPE_VOIDPTR
 *
 * Vevo will complain about:
 *        -# Ports that have never been freed
 *        -# Ports that have not been allocated by vevo
 *        -# Ports that have already been freed
 *        
 * This library is not complete, it contains some loose ends:
 *   - sscanf/ printf
 *   - vevo_num_properties
 *         
*/

#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libvevo/vevo.h>
#include <libhash/hash.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-common.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#include	<libvevo/pool.h>
#define PORT_TYPE_PLUGIN_INFO 1
#define PORT_TYPE_FILTER_CLASS 2
#define PORT_TYPE_FILTER_INSTANCE 3
#define PORT_TYPE_CHANNEL_TEMPLATE 4
#define PORT_TYPE_PARAMETER_TEMPLATE 5
#define PORT_TYPE_CHANNEL 6
#define PORT_TYPE_PARAMETER 7
#define PORT_TYPE_GUI 8


//! \typedef atom_t
/*! \brief atom
 * 
 *  the atom_t structure
 */
typedef struct {
    int type;
    void *value;
    size_t size;
} atom_t;

//! \typedef vevo_storage_t 
/*! \brief atom storage
 * 
 * 
 *  the vevo_storage_t structure holds one or more elements
 *  and some bookkeeping information
 */
typedef struct {
    int atom_type; /*!< atom type */
    union { /*! \union elements one or more atoms */
	atom_t *atom; 
	atom_t **array;
    } elements;
    int num_elements; /*!< number of atoms */
    int flags;	      /*!< special flags */
    int softlink;	/*! < protected, use to indicate soft link */
} vevo_storage_t;

//! \typedef vevo_property_t
/*! \brief tupple of key | object storage 
 *
 *
 * the vevo_property_t structure binds a key to a vevo_storage_t structure
 */
typedef struct {
    vevo_storage_t *st; 
    uint64_t key;
    void *next;
} vevo_property_t;

//! \typedef port_index_t
/*! \brief mnemonics of key | hash value pairs
 *
 *
 *  the port_index_t keeps a mnemonic of key to hash code
 */
typedef struct {
    const char *key;
    uint64_t hash_code;		/* vevo uses a integer representation of key, eliminates strcmp  */
    void *next;
} port_index_t;

//! \typedef __vevo_port_t
/*! \brief top level container
 *
 * 
 * the __vevo_port_t is the top level structure
 */
typedef struct {
    hash_t *table;		/*!< hashtable to store pairs of key | value */
    vevo_property_t *list;	/*!< linked list to store pairs of key | value */
    port_index_t *index;	/*!< mnemonic table of key to hashcode */
    void	*pool;		/*!< pointer to pool_t object caching */
} __vevo_port_t;


//! \var port_ref_ Book keeping of allocated and freed ports
#ifdef STRICT_CHECKING
static	vevo_port_t	*port_ref_ = NULL;
#endif
static  size_t		atom_sizes_[100];

//! Check if an object is soft referenced
/*!
 \param p port
 \param name of object
 \return TRUE if an object is soft referenced
 */ 
static int vevo_property_is_soft_referenced(vevo_port_t * p, const char *key );

//! Recursivly free all ports
/*!
\param sorted_port port to collect all atoms of type VEVO_ATOM_TYPE_PORTPTR in port p
 \param p input port
 */
static void vevo_port_recurse_free( vevo_port_t *sorted_port, vevo_port_t *p );
static char *vevo_scan_token_( const char *s );
static const char *vevo_split_token_q( const char *s, const char delim, char *buf, int buf_len );


static const char *vevo_split_token_( const char *s, const char delim, char *buf, int buf_len );


//! Construct a new vevo_property_t
/*!
 \param port port
 \param hash_key hash value
 \param stor vevo_storage_t
 */
static vevo_property_t *prop_node_new(__vevo_port_t *port, uint64_t hash_key,
					vevo_storage_t * stor)
{
    vevo_property_t *p =
		(vevo_property_t*) vevo_pool_alloc_property( vevo_property_t, port->pool );
   	    //   (vevo_property_t *) vevo_malloc(sizeof(vevo_property_t));
    p->st = stor;
    p->key = hash_key;
    p->next = NULL;
    return p;
}

//! Destroy a property
/*!
 \param port port
 \param p property to free
 */
static void prop_node_free(__vevo_port_t *port,vevo_property_t * p)
{
    if (p) {
       //free(p);
       vevo_pool_free_property( port->pool, p );
    }
    p = NULL;
}
//! Append a property to the linked list of properties
/*!
 \param p port 
 \param key hash value
 \param t vevo_storage_t
 */
static vevo_property_t *prop_node_append(vevo_port_t * p, uint64_t key,
					   vevo_storage_t * t)
{
    __vevo_port_t *port = (__vevo_port_t *) p;
    vevo_property_t *node = prop_node_new(port,key, t);
    vevo_property_t *next;
    vevo_property_t *list = port->list;
    if (list == NULL)
	port->list = node;
    else {
	while (list != NULL) {
	    next = list->next;
	    if (next == NULL) {
		list->next = node;
		return node;
	    }
	    list = next;
	}
    }
    return node;
}

//! Get a property from a port
/*!
 \param port
 \param key hash value
 */
static vevo_property_t *prop_node_get(vevo_port_t * p, uint64_t key)
{
    __vevo_port_t *port = (__vevo_port_t *) p;
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif
    vevo_property_t *l = port->list;
    while (l != NULL) {
	if (key == l->key)
	    return l;
	l = l->next;
    }
    return NULL;
}

//! Construct a new mnemonic  
/*!
 \param port port
 \param key name of property
 \param hash_key calculated hash value
 \return port_index_t new mmemonic
 */
static port_index_t *port_node_new(__vevo_port_t *port,const char *key, uint64_t hash_key)
{
 //   port_index_t *i = (port_index_t *) vevo_malloc(sizeof(port_index_t));
	port_index_t *i = (port_index_t *) vevo_pool_alloc_node(
			port_index_t, port->pool );
#ifdef STRICT_CHECKING
    assert(i != NULL);
    assert(key != NULL);
    assert(hash_key != 0);
#endif
    i->key = strdup(key);
    i->hash_code = hash_key;
    i->next = NULL;
    return i;
}

//! Destroy a mnemonic from the list of key | hash value pairs
/*!
 \param port port
 \param node mnemonic to destroy
 */
static void port_node_free(__vevo_port_t *port,port_index_t * node)
{
    if (node) {
	if (node->key)
    		free((void *) node->key);
//	free(node);
	vevo_pool_free_node( port->pool,(void*)node );
    }
    node = NULL;
}

//! Add a mnemonic to the list of key | hash value pairs
/*!
 \param p port
 \param key name of property
 \param hash_key calculated hash value
 */
static void port_node_append(vevo_port_t * p, const char *key,
				    uint64_t hash_key)
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert(key != NULL);
    assert(hash_key != 0);
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    port_index_t *node = port_node_new(p,key, hash_key);
    port_index_t *next;
    port_index_t *list = port->index;
    if (list == NULL)
	port->index = node;
    else {
	while (list != NULL) {
	    next = list->next;
	    if (next == NULL) {
		list->next = node;
		break;
	    }
	    list = next;
	}
    }
}

#define PROPERTY_KEY_SIZE	128

//! \define property_exists Check if a property exists
#define property_exists( port, key ) hash_lookup( (hash_t*) port->table, (const void*) key )

//! \define atom_store__ store atom
#define atom_store__(value) {\
for (i = 0; i < d->num_elements; i++)\
 d->elements.array[i] = vevo_put_atom(port, &value[i], v ); }

//! \define array_load__ get a value from an array of atom
#define array_load__(value) {\
for( i = 0; i < t->num_elements ; i ++ )\
 memcpy( &value[i], t->elements.array[i]->value, t->elements.array[i]->size ); }

//! Construct a new vevo_storage_t object
/*!
 \param port port
 \return vevo_storage_t a new vevo_storage_t object
*/
static vevo_storage_t *vevo_new_storage(__vevo_port_t *port );

static int	vevo_port_ref_verify( vevo_port_t *p) ;
//! Copy a value into a new atom
/*!
 \param port port
 \param src address of value to store
 \param n number of elements to store
 \param d vevo_storage_t destination
 \param v atom type
 */
static void
storage_put_atom_value(__vevo_port_t * port, void *src, int n,
		       vevo_storage_t * d, int v);

//! Calculate a hash value from a given string
/*!
 \param str string
 \return calculated hash value
 */
static inline uint64_t hash_key_code( const char *str )   	
{
        uint64_t hash = 5381;
        int c;

        while ((c = *str++))
            hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

        return hash;
}

//! Flag a property as soft referenced. Use to avoid freeing a linked pointer.
/*!
 \param p port
 \param key property to soft reference
 \return error code
 */
int vevo_property_soft_reference(vevo_port_t * p, const char *key)
{
    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);
    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	    node->st->softlink = 1;
	    return VEVO_NO_ERROR;
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
	    stor->softlink = 1;
	    hnode_put( node, (void*) hash_key );
	    return VEVO_NO_ERROR;
	}

    }

    return VEVO_NO_ERROR;
}


//! Flag a property as read-only. Use to avoid overriding the value
/*!
 \param p port
 \param key property to set as readonly
 \return error code
 */
static int vevo_property_finalize(vevo_port_t * p, const char *key)
{
    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);
    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	    node->st->flags |= VEVO_PROPERTY_READONLY;
	    return VEVO_NO_ERROR;
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
	    stor->flags |= VEVO_PROPERTY_READONLY;
	    hnode_t *new_node = hnode_create((void *) stor);
	    hnode_put(new_node, (void *) hash_key);
	    hnode_destroy(new_node);
	    return VEVO_NO_ERROR;
	}
    }
    return VEVO_NO_ERROR;
}

static int vevo_property_exists( vevo_port_t *p, const char *key)
{
	__vevo_port_t *port = (__vevo_port_t *) p;

    	uint64_t hash_key = hash_key_code(key);
	if (!port->table) {
		vevo_property_t *node = NULL;
		if ((node = prop_node_get(port, hash_key)) != NULL) 
			return 1;
    	} else {
		hnode_t *node = NULL;
		if ((node = property_exists(port, hash_key)) != NULL) 
			return 1;
	}    	
	return 0;
}

//! Local add, add a property to the list of properties and finalize if needed
/*!
 \param p port
 \param finalize set property readonly TRUE or FALSE
 \param key property name
 \param atom_type atom type
 \param num_elements number of atoms
 \param src address of source variable
 */
static	void	vevo_port_add_property( vevo_port_t *p,int finalize, const char *key,int atom_type, int num_elements, void * src )
{
	__vevo_port_t *port = (__vevo_port_t *) p;

	uint64_t hash_key = hash_key_code(key);
	vevo_storage_t *stor = vevo_new_storage(p);
	storage_put_atom_value(port, src, num_elements, stor, atom_type);
	if(finalize)
		stor->flags |= VEVO_PROPERTY_READONLY;
	port_node_append(port, key, hash_key);
	if (!port->table)
	    prop_node_append(port, hash_key, stor);
	else
		hash_insert( (hash_t*) port->table,
			hnode_create(stor),(const void*) hash_key );    
}

//! Finalize a port. Add it to the reference list and register port type
/*!
 \param port port to finalize
 \param port_type port type
 */
static	void	vevo_port_finalize( vevo_port_t *port, int port_type )
{
#ifdef STRICT_CHECKING
	if( port_type != VEVO_PORT_REFERENCES )
	{
		int ref = 1;
		char ref_key[64];
		sprintf(ref_key,"%p",port );
		vevo_property_set( port_ref_, ref_key, VEVO_ATOM_TYPE_INT, 1, &ref );
	}
#endif
	if( port_type <= 1024 && port_type > 0 )
		vevo_port_add_property( port, 1,"type",VEVO_ATOM_TYPE_INT,1, &port_type );
}	

//! Copy a value from an atom to an address
/*!
 \param t vevo_storage_t source
 \param idx element at
 \param dst destination address
 \return error code
 */


static int atom_get_value(vevo_storage_t * t, int idx, void *dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
#endif
    atom_t *atom = NULL;

    if (t->num_elements == 1 && idx == 0)
    {
    	    atom = t->elements.atom;
#ifdef STRICT_CHECKING
	    assert( atom != NULL );
#endif
    }
	    
    if (t->num_elements > 1 && idx >= 0 && idx <= t->num_elements)
    {
    	    atom = t->elements.array[idx];
#ifdef	STRICT_CHECKING
	    assert( atom != NULL );
#endif
    }
   
    if( t->num_elements == 0 && idx == 0 )
    {
	    atom = t->elements.atom;
#ifdef STRICT_CHECKING
	    assert( atom != NULL );
#endif
	    return VEVO_ERROR_PROPERTY_EMPTY;
    }
    
    
    if (!atom)
    	return VEVO_ERROR_NOSUCH_ELEMENT;
    
    if (atom->size <= 0)
	return VEVO_NO_ERROR;
  
    if( dst == NULL )
	    return VEVO_NO_ERROR;
    
    if (t->atom_type != VEVO_ATOM_TYPE_STRING) {
#ifdef STRICT_CHECKING
	assert( atom->size > 0 );
#endif
	memcpy(dst, atom->value, atom->size);
    } else {
	char **ptr = (char **) dst;
	char *p = *ptr;
#ifdef STRICT_CHECKING
	assert( (atom->size-1) > 0 );
#endif
	memcpy(p, atom->value, (atom->size - 1));
	p[atom->size - 1] = '\0';
    }
    return VEVO_NO_ERROR;
}

//! Get the byte size of an atom
/*!
 \param atom_type atom type
 \return byte size
 */
static size_t vevo_atom_size(int atom_type)
{
	return atom_sizes_[atom_type];
    return 0;
}

//! Construct a new Atom 
/*!
 \param port port 
 \param atom_type type of atom to construct
 \param atom_size byte size of atom
 \return atom_t new atom
 */
static atom_t *vevo_new_atom(__vevo_port_t * port, int atom_type,
			       size_t atom_size)
{
    atom_t *atom = (atom_t *) vevo_pool_alloc_atom( atom_t , port->pool );
#ifdef STRICT_CHECKING
    assert(atom != NULL);
#endif
    atom->size = atom_size;
    atom->type = atom_type;
    if(atom_type == VEVO_ATOM_TYPE_STRING )
   	 atom->value = (atom_size > 0 ?(void*)malloc(atom_size):NULL);	
    else
    {	 if(atom_type == VEVO_ATOM_TYPE_DOUBLE ) {
		 atom->value = (atom_size > 0 ? (void*) vevo_pool_alloc_dbl(double,port->pool ): NULL);
	 } else if( atom_type == VEVO_ATOM_TYPE_VOIDPTR || atom_type == VEVO_ATOM_TYPE_PORTPTR ) {
		 atom->value = (atom_size > 0 ? (void*) vevo_pool_alloc_ptr(void*,port->pool ): NULL );
         } else if( atom_type == VEVO_ATOM_TYPE_UINT64 ) {
		 atom->value = (atom_size > 0 ? (void*) vevo_pool_alloc_64b(uint64_t,port->pool) : NULL );
	 } else {
		atom->value = (atom_size > 0 ? (void*) vevo_pool_alloc_int(int32_t, port->pool ): NULL );
	 }
    } 
#ifdef STRICT_CHECING
    assert(atom != NULL);
#endif
    return atom;
}

//! Destroy an atom
/*!
 \param port port
 \param atom atom_t to destroy
 */
static void vevo_free_atom(__vevo_port_t *port,atom_t * atom)
{
#ifdef STRICT_CHECKING
    assert(atom != NULL);
#endif
    if (atom) {
	if(atom->value)
		switch( atom->type )
		{
			case VEVO_ATOM_TYPE_VOIDPTR:
			case VEVO_ATOM_TYPE_PORTPTR:
				vevo_pool_free_ptr( port->pool, atom->value );
				break;
			case VEVO_ATOM_TYPE_INT:
			case VEVO_ATOM_TYPE_BOOL:
				vevo_pool_free_int( port->pool, atom->value );
				break;
			case VEVO_ATOM_TYPE_DOUBLE:
				vevo_pool_free_dbl( port->pool,atom->value );
				break;
			case VEVO_ATOM_TYPE_UINT64:
				vevo_pool_free_64b( port->pool,atom->value );
				break;
			case VEVO_ATOM_TYPE_STRING:
				free( atom->value );
				break;
		}
	vevo_pool_free_atom( port->pool, atom );
    }
    atom = NULL;
}

//! Copy a value from address into a new Atom
/*!
 \param port port
 \param dst destination address
 \param atom_type type of atom
 */
static atom_t *vevo_put_atom(__vevo_port_t * port, void *dst,
			       int atom_type)
{
    atom_t *atom = NULL;
    size_t atom_size = vevo_atom_size(atom_type);

    if (atom_type == VEVO_ATOM_TYPE_STRING) {
	char **s = (char **) dst;
	atom_size = strlen(*s) + 1;
	atom = vevo_new_atom(port, atom_type, atom_size);
	if (atom_size > 0)
	   memcpy(atom->value, *s, (atom_size - 1));
    } else {

#ifdef STRICT_CHECKING
	assert(atom_size > 0);
	assert(dst != NULL);
#endif
	atom = vevo_new_atom(port, atom_type, atom_size);
#ifdef STRICT_CHECING
	assert(atom != NULL);
	assert(atom->value != NULL);
#else
	if (!atom)
	    return NULL;
#endif
	memcpy(atom->value, dst, atom_size);
    }
    return atom;
}


//! Copy N values from address into a new Atom
/*!
 \param port port
 \param src source address
 \param n number of elements
 \param d vevo_storage_t
 \param v type of atom
 */

static void
storage_put_atom_value(__vevo_port_t * port, void *src, int n,
		       vevo_storage_t * d, int v)
{
    int i;
#ifdef STRICT_CHECKING
    if (n > 0)
	assert((src != NULL));
#endif
    if (d->num_elements >= 0) {
	if (d->num_elements >= 0 && d->num_elements <= 1) {
#ifdef STRICT_CHECKING
		if( d->num_elements == 1 )
			assert( d->elements.atom != NULL );
		if( d->num_elements == 0 )
			assert( d->elements.atom == NULL );
#endif
    		if (d->elements.atom)
			vevo_free_atom(port,d->elements.atom);
	} else if (d->num_elements > 1) {
	    if (d->elements.array) {
		for (i = 0; i < d->num_elements; i++)
		    vevo_free_atom(port,d->elements.array[i]);
		free(d->elements.array);
	    }
	}
    }

    d->atom_type = v;
    d->num_elements = n;

    switch (n) {
    case 0:
	d->elements.atom = vevo_new_atom(port, v, vevo_atom_size(v));
	break;
    case 1:
	d->elements.atom = vevo_put_atom(port, src, v);
	break;
    default:
	d->elements.array = (atom_t **) malloc(sizeof(atom_t *) * n);
	if (d->atom_type == VEVO_ATOM_TYPE_DOUBLE) {
	    double *value = (double *) src;
	    atom_store__(value);
	} else {
	    if (d->atom_type == VEVO_ATOM_TYPE_INT
		|| d->atom_type == VEVO_ATOM_TYPE_BOOL) {
		int32_t *value = (int *) src;
		atom_store__(value);
	    } else if(d->atom_type == VEVO_ATOM_TYPE_UINT64) {
		    uint64_t *value = (uint64_t*) src;
		    atom_store__(value);
	    } else {
		void **value = (void **) src;
		atom_store__(value);
	    }
	}
	break;
    }
}

//! Construct a new vevo_storage_t object
/*!
 \param port Port
 \return vevo_storage_t
 */
static vevo_storage_t *vevo_new_storage( __vevo_port_t *port)
//static inline vevo_storage_t *vevo_new_storage(int num_elements)
{
    vevo_storage_t *d =
	    (vevo_storage_t*) vevo_pool_alloc_storage( vevo_storage_t, port->pool );
//	(vevo_storage_t *) vevo_malloc(sizeof(vevo_storage_t));
#ifdef HAVE_STRICT
    assert(d != NULL);
#endif
    memset( d, 0, sizeof(vevo_storage_t));
    return d;
}

//! Destroy a vevo_storage_t object
/*!
 \param port port
 \param t vevo_storage_t to destroy
 */
static void vevo_free_storage(__vevo_port_t *port,vevo_storage_t * t)
{
    if (t) {
	if (t->num_elements > 1) {
	    int i;
	    for (i = 0; i < t->num_elements; i++)
	    {
#ifdef STRICT_CHECKING
			assert( t->elements.array[i] != NULL );
#endif			
	    	    vevo_free_atom(port,t->elements.array[i]);
	    }
	    free(t->elements.array);
	}
	if (t->num_elements <= 1)
	{
#ifdef STRICT_CHECKING
		assert( t->elements.atom != NULL );
#endif	
		    vevo_free_atom(port,t->elements.atom);

        }
//	free(t);
	vevo_pool_free_storage( port->pool, t );
    }
    t = NULL;
}

//! Cast a key into a hashing value
/*!
 \param key hash key code
 \return hash value
 */
static hash_val_t int_hash(const void *key)
{
    return (hash_val_t) key;
}

//! Compare two keys
/*!
 \param key1 key1
 \param key2 key2
 \return error code if keys are unequal
 */
static int key_compare(const void *key1, const void *key2)
{
    return ((const uint64_t) key1 == (const uint64_t) key2 ? 0 : 1);
}

//! Get the number of elements in an Atom
/*!
 \param p port
 \param key property name
 \return Number of elements
 */
int vevo_property_num_elements(vevo_port_t * p, const char *key)
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert(key != NULL);
#endif
    if(!p) return -1;

    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);

    if (!port->table) {
	vevo_property_t *node;
	if ((node = prop_node_get(port, hash_key)) != NULL)
	    return node->st->num_elements;
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
	    if (stor)
		return stor->num_elements;
	}
    }
    return -1;
}

//! Get the atom type of a Property
/*!
 \param p Port
 \param key Property name
 \return Atom type
 */
int vevo_property_atom_type(vevo_port_t * p, const char *key)
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert(key != NULL);
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
#ifdef STRICT_CHECKING
    assert(port != NULL);
#endif
    uint64_t hash_key = hash_key_code(key);

    if (!port->table) {
	vevo_property_t *node;
	if ((node = prop_node_get(port, hash_key)) != NULL)
	    return node->st->atom_type;
    } else {
	hnode_t *node = NULL;
#ifdef STRICT_CHECKING
	assert(port->table != NULL);
//	assert(hash_verify(port->table) != 0);
#endif

	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
	    if (stor)
		return stor->atom_type;
	}
    }
    return -1;
}

//! Return size of an Atom at a given index
/*!
 \param p Port
 \param key Property name
 \param idx Index
 \return Byte size of value at Index
 */
size_t
vevo_property_element_size(vevo_port_t * p, const char *key,
			     const int idx)
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert(key != NULL);
    assert(idx >= 0);
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);
#ifdef STRICT_CHECKING
    assert( port );
#endif
    if (!port->table) {
	vevo_property_t *node;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
#ifdef STRICT_CHECKING
	    if (idx > 0)
		assert(idx < node->st->num_elements);
#endif
	    if (node->st->num_elements == 1)
		return node->st->elements.atom->size;
	    else if (node->st->num_elements > 1)
		return node->st->elements.array[idx]->size;
	    return 0;
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
#ifdef STRICT_CHECKING
	    assert(idx <= stor->num_elements);
#endif
	    //todo: sum all element sizes for index of -1 
	    if (stor->num_elements == 1) {
		return stor->elements.atom->size;
	    } else if (stor->num_elements > 1) {
		return stor->elements.array[idx]->size;
	    } else {
		if (stor->num_elements == 0)
		    return 0;
	    }
	}
    }

    return -1;
}

//! Construct a new Port 
/*!
 \param port_type Type of Port to Create. Port types <= 1024 are typed, > 1024 are anonymous ports.
 \return A New Port
 */
#ifndef STRICT_CHECKING
vevo_port_t *vevo_port_new(int port_type)
{
    __vevo_port_t *port = (__vevo_port_t *) malloc(sizeof(__vevo_port_t));

    port->index = NULL;
    port->list = NULL;
    port->table = NULL;
    port->pool =  vevo_pool_init( sizeof(vevo_property_t),sizeof( vevo_storage_t ), sizeof( atom_t ) , sizeof( port_index_t ) );
/* If the port type is a Livido port this or that */
    if ( (port_type >= 1 && port_type <= 50) || port_type < 0)
	port->list = NULL;
    else
	port->table = hash_create(HASHCOUNT_T_MAX, key_compare, int_hash);

    vevo_port_finalize (port, port_type );
    return (vevo_port_t *) port;
}
#else
vevo_port_t *vevo_port_new(int port_type, const char *func, int line_num)
{
    __vevo_port_t *port = (__vevo_port_t *) malloc(sizeof(__vevo_port_t));

#ifdef STRICT_CHECKING
    assert(port != NULL);
#endif
    port->index = NULL;
    port->list = NULL;
    port->table = NULL;
    port->pool =  vevo_pool_init( sizeof(vevo_property_t),sizeof( vevo_storage_t ), sizeof( atom_t ) , sizeof( port_index_t ) );
/* If the port type is a Livido port this or that */
    if ( (port_type >= 1 && port_type <= 50) || port_type < 0)
	port->list = NULL;
    else
	port->table = hash_create(HASHCOUNT_T_MAX, key_compare, int_hash);

    vevo_port_finalize (port, port_type );

/*    char har[1024];
    sprintf(har,"%s::%d", func,line_num );
	vevo_property_set( port, har, VEVO_ATOM_TYPE_HIDDEN,0,NULL);
  */  
    return (vevo_port_t *) port;
}

#endif

//! Initialize VeVo. Set up bookkeeping information to track Port construction and destruction
void	vevo_strict_init()
{
#ifdef STRICT_CHECKING
	port_ref_ = vevo_port_new( VEVO_PORT_REFERENCES, __FUNCTION__,__LINE__ );
#endif
	memset( atom_sizes_,0,sizeof(atom_sizes_) );
	atom_sizes_[1] = sizeof(int32_t);
	atom_sizes_[2] = sizeof(double);
	atom_sizes_[3] = sizeof(int32_t);
	atom_sizes_[4] = sizeof(char*);
	atom_sizes_[5] = sizeof(uint64_t);
	atom_sizes_[65] = sizeof(void*);
	atom_sizes_[66] = sizeof(vevo_port_t*);
}

//! Destroy a Port
/*!
 \param p (local) Port to destroy
 */
static void vevo_port_free_(vevo_port_t * p)
{
    __vevo_port_t *port = (__vevo_port_t *) p;

    if (port->table) {
	if (!hash_isempty((hash_t *) port->table)) {
		hscan_t scan;
		hash_scan_begin(&scan, (hash_t *) port->table);
		hnode_t *node;

		while ((node = hash_scan_next(&scan)) != NULL) {
		    vevo_storage_t *stor = NULL;
		    stor = hnode_get(node);
#ifdef STRICT_CHECKING
		    assert(stor != NULL);
		    assert(node != NULL);
		    assert((const char *) hnode_getkey(node) != NULL);
#endif
		    vevo_free_storage(port,stor);
		}
		hash_free_nodes((hash_t *) port->table);
		hash_destroy((hash_t *) port->table);
	    }
    }
    else
    {
	    vevo_property_t *l = port->list;
	    vevo_property_t *n;
	    while (l != NULL) {
		n = l->next;
		vevo_free_storage(port,l->st);
		prop_node_free(port,l);
		l = n;
	    }
     }

     port_index_t *l = port->index;
     port_index_t *n = NULL;
     while (l != NULL) {
	n = l->next;
	port_node_free(port,l);
	l = n;
     }
    
     vevo_pool_destroy( port->pool );	
     
     free(port);
     p = port = NULL;
}

//! Verify if Vevo has allocated a given Port
/*!
 \param port Port to verify
 \return error code
 */
int	vevo_port_verify( vevo_port_t *port )
{
#ifdef STRICT_CHECKING
	if( port == port_ref_ )
		return 1;
	char pkey[32];
	sprintf(pkey, "%p",port);

	int ref_count = 0;
	int error =	vevo_property_get( port_ref_, pkey, 0, &ref_count );

	if( error != 0 )
	{
		veejay_msg(0, "%s: Port '%s' not allocated by vevo_port_new()", __FUNCTION__, pkey );
		return 0;
	}

	if( error == VEVO_ERROR_NOSUCH_ELEMENT || error == 
	 	VEVO_ERROR_NOSUCH_PROPERTY )
	{
		veejay_msg(0, "%s: Port '%s' does not exist" );
		return 0;
	}
	
	if( ref_count == 0 )
	{
		veejay_msg(0, "%s: Port %s has a reference count of 0 ",__FUNCTION__, pkey);
		return 0;
	}
#endif
	return 1;
}

//! Destroy a Port
/*!
 \param p Port to destroy
 */

void	vevo_port_free( vevo_port_t *port )
{
	int ref_count = 1;
	int dec_ref   = 1;
	int error = 0;

	if(!port)
	{
		veejay_msg(0, "Port invalid in free()");
#ifdef STRICT_CHECKING
		assert(0);
#endif
		return;
	}
#ifdef STRICT_CHECKING
	char pkey[32];
	sprintf(pkey, "%p",port);
	if( port == port_ref_ )
	{
		dec_ref = 0;
		vevo_port_free_( port );
	}
	if( dec_ref)
	{
		error =	vevo_property_get( port_ref_, pkey, 0, &ref_count );
		if( error != VEVO_NO_ERROR )
		{
			veejay_msg(0, "%s: Port '%s' not allocated by vevo_port_new()", __FUNCTION__, pkey );
			assert(0);
		}
		if( ref_count == 0 )
		{
			veejay_msg(0, "%s: Port '%s' has a reference count of 0 (already freed)", __FUNCTION__, pkey );
			assert(0);
		}
		if( ref_count > 1 )
		{
			veejay_msg(0, "%s: Port '%s' has a reference count of %d", __FUNCTION__,pkey,ref_count);
			return;
		}
		ref_count --;
		assert( ref_count == 0 );
		vevo_property_set( port_ref_, pkey, VEVO_ATOM_TYPE_INT,1,&ref_count );
		vevo_port_free_( port );
	}
#else
	vevo_port_free_(port );
#endif	
}

//! Check if a Property is soft referenced
/*!
 \param p Port
 \param key Property name
 \return error code
 */
static int
vevo_property_is_soft_referenced(vevo_port_t * p,
		    const char *key )
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert( key != NULL );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);
#ifdef STRICT_CHECKING
    assert( port != NULL );
#endif
    if (!port->table) {
	vevo_property_t *pnode = NULL;
	if ((pnode = prop_node_get(port, hash_key)) != NULL) {
	    return pnode->st->softlink;
	}
    } else {
	hnode_t *old_node = NULL;
	if ((old_node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *oldstor =
		(vevo_storage_t *) hnode_get(old_node);
	    return oldstor->softlink;
	}
    }
    return 0;
}    

//! Store a value as a new Property or overwrite existing value
/*!
 \param p Port
 \param key Property name
 \param atom_type Atom type
 \param num_elements Number of elements
 \param src Source address
 \return error code
 */
int
vevo_property_set(vevo_port_t * p,
		    const char *key,
		    int atom_type, int num_elements, void *src)
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
	//@ no self referencing
    assert( src != p );
    if( num_elements > 0 ) assert( src != NULL );
    assert( key != NULL );
    if( atom_type != VEVO_ATOM_TYPE_HIDDEN )
  	  assert( vevo_atom_size(atom_type) > 0 );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);
    int new = 1;
    void *node = NULL;
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif
    if (!port->table) {
	vevo_property_t *pnode = NULL;
	if ((pnode = prop_node_get(port, hash_key)) != NULL) {
#ifdef STRICT_CHECKING
    	    if (pnode->st->atom_type != atom_type)
		return VEVO_ERROR_WRONG_ATOM_TYPE;
	    if (pnode->st->flags & VEVO_PROPERTY_READONLY)
	    {
		return VEVO_ERROR_PROPERTY_READONLY;
	    }
#endif
	    vevo_free_storage(port,pnode->st);
	    //prop_node_free(pnode);
	    new = 0;
	    node = (void *) pnode;
	}
    } else {
	hnode_t *old_node = NULL;
	if ((old_node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *oldstor =
		(vevo_storage_t *) hnode_get(old_node);
#ifdef STRICT_CHECKING
	    if (oldstor->atom_type != atom_type)
		return VEVO_ERROR_WRONG_ATOM_TYPE;

	    if (oldstor->flags & VEVO_PROPERTY_READONLY)
		return VEVO_ERROR_PROPERTY_READONLY;
#endif
	    vevo_free_storage(port,oldstor);

	    hash_delete((hash_t *) port->table, old_node);
	    hnode_destroy(old_node);
	    new = 0;
	}
    }
    vevo_storage_t *stor = vevo_new_storage(port);
    
    storage_put_atom_value(port, src, num_elements, stor, atom_type);

    if (new) {
	port_node_append(port, key, hash_key);
	if (!port->table)
	    node = (void *) prop_node_append(port, hash_key, stor);
    }
#ifdef STRICT_CHECKING
    assert(stor != NULL);
#endif

    if (!port->table) {
#ifdef STRICT_CHECKING
	assert(node != NULL);
#endif
	if (!new) {
	    vevo_property_t *current = (vevo_property_t *) node;
	    current->st = stor;
	}
    } else {
	hnode_t *node2 = hnode_create(stor);

#ifdef STRICT_CHECKING
	assert(node2 != NULL);
	assert(!hash_isfull((hash_t *) port->table));
	assert(!property_exists(port, hash_key));
#endif

	hash_insert((hash_t *) port->table, node2,
		    (const void *) hash_key);
    }

    return VEVO_NO_ERROR;
}

//! Get a value from a Property
/*!
 \param p Port
 \param key Property name
 \param idx Index
 \param dst Destination address
 \return error code
 */
int
vevo_property_get(vevo_port_t * p, const char *key, int idx, void *dst)
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert( key != NULL );
    assert( idx >= 0 );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);

    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
		return atom_get_value(node->st, idx, dst);
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
		return atom_get_value((vevo_storage_t *) hnode_get(node),
				      idx, dst);
	}
    }

    return VEVO_ERROR_NOSUCH_PROPERTY;
}

//! List all properties in a Port
/*!
 \param p Port
 \return Null terminated list of property names
 */
char **vevo_list_properties(vevo_port_t * p)
{
    if(!p) return NULL;

    __vevo_port_t *port = (__vevo_port_t *) p;
    if(!port->index)
		return NULL;
#ifdef STRICT_CHECKING
    assert(port != NULL);
#endif
    char **list = NULL;

#ifdef STRICT_CHECKING
    int nn = 1;
    if (port->table)
	nn += hash_count((hash_t *) port->table);
    else {
	vevo_property_t *c = port->list;
	while (c != NULL) {
	    c = c->next;
	    nn++;
	}
    }
#endif

    int n = 1;			// null terminated list of keys
    int i = 0;
#ifdef STRICT_CHECKING
    assert( port->index != NULL );
#endif
    port_index_t *l = port->index;
    while (l != NULL) {
	l = l->next;
	n++;
    }

#ifdef STRICT_CHECKING
    if( n != nn )
    veejay_msg(0, "%s:%s (%p) Expected %d properties but there are %d",
		    __FILE__,__FUNCTION__,p,nn, n );
    assert(nn == n);
#endif

    list = (char **) malloc(sizeof(char *) * n);
    if (!list)
	return NULL;

    l = (port_index_t *) port->index;
    i = 0;
    while (l != NULL) {
#ifdef STRICT_CHECKING
	if( vevo_property_atom_type( p, l->key ) != VEVO_ATOM_TYPE_HIDDEN )
	{
#endif
        list[i] = (char *) strdup(l->key);
#ifdef STRICT_CHECING
	assert(list[i] != NULL);
#endif
	i++;
#ifdef STRICT_CHECKING
	}
#endif

	l = l->next;
	//i++;
    }
    list[i] = NULL;

    return list;
}

int	vevo_num_properties(vevo_port_t * p)
{
     __vevo_port_t *port = (__vevo_port_t *) p;
    if(!port->index)
		return 0;

    int n = 0;
    port_index_t *l = port->index;
    while (l != NULL) {
	l = l->next;
	n++;
    }

    return n;
}


//! Check if this Port holds an Atom of some type
/*!
 \param p Port
 \param atype Atom type
 \return error code
*/ 
static	int	vevo_scan_for_atom( vevo_port_t *p, int atype )
{
    __vevo_port_t *port = (__vevo_port_t *) p;
    if(!p) return 0;
   
    if( port->table)
    {
	hnode_t *node;
	hscan_t scan;
	vevo_storage_t *s;	
#ifdef STRICT_CHECKING
	assert( port->table != NULL );
#endif
	hash_scan_begin( &scan,(hash_t*) port->table );

	while((node=hash_scan_next(&scan)) != NULL)
	{
		s = hnode_get(node);
		if( s->atom_type == atype )
			return 1;
	}
    }
    else
    {
 	vevo_property_t *l = port->list;
	vevo_property_t *n;
	vevo_storage_t *s;
	while( l != NULL )
	{
		n = l->next;
		s = l->st;

		if( s->atom_type == atype )
			return 1;
	
		l = n;
	}
    }
    return 0;
}

//! List all Properties in a Port that match Atom type
/*!
 \param p Port
 \param atype Atom type
 \return List of vevo_storage_t
 */
static vevo_storage_t **vevo_list_nodes_(vevo_port_t * p, int atype)
{
    __vevo_port_t *port = (__vevo_port_t *) p;
    if(!p) return NULL;
    
    int n = 256;			// null terminated list of keys
    int i = 0;

    vevo_storage_t **list = (vevo_storage_t**)malloc(sizeof(vevo_storage_t*) * n );
	memset(list,0,sizeof(vevo_storage_t*) * n );

    if( port->table)
    {
	hnode_t *node;
	hscan_t scan;
	vevo_storage_t *s;	
	hash_scan_begin( &scan,(hash_t*) port->table );
	while((node=hash_scan_next(&scan)) != NULL)
	{
		s = hnode_get(node);
		if( s && s->atom_type == atype || atype == 0)
		{
			int type = 0;
			int ec = atom_get_value(s, 0, &type);
		/*	if( type != PORT_TYPE_FILTER_CLASS ||
				type != PORT_TYPE_CHANNEL_TEMPLATE ||
				 	type != PORT_TYPE_PARAMETER_TEMPLATE ||
						type != PORT_TYPE_PLUGIN_INFO ||
			 	!(s->flags & VEVO_PROPERTY_SOFTREF) )*/

			if( ec == VEVO_NO_ERROR && !s->softlink )
				list[i++] = s;
		}
	}
    }
    else
    {
	vevo_property_t *l = port->list;
	vevo_property_t *n;
	vevo_storage_t *s;
	while( l != NULL )
	{
		n = l->next;
		s = l->st;

		if( s->atom_type == atype || atype == 0 )
		{
			int type = 0;
			int ec = atom_get_value(l->st, 0, &type);
		/*	if( (type != PORT_TYPE_FILTER_CLASS ||
				type != PORT_TYPE_CHANNEL_TEMPLATE ||
				 	type != PORT_TYPE_PARAMETER_TEMPLATE ||
						type != PORT_TYPE_PLUGIN_INFO ) &&
					ec != VEVO_ERROR_PROPERTY_EMPTY &&
						!(s->flags & VEVO_PROPERTY_SOFTREF))
		*/
			if( ec == VEVO_NO_ERROR && !s->softlink )
				list[i++] = s;
		}
		l = n;
	}
    }
#ifdef STRICT_CHECKING
    assert( i < 256 );
#endif
    return list;
}

//! Report statistics and free bookkeeping information
void	vevo_report_stats()
{
#ifdef STRICT_CHECKING
	if( port_ref_ )
	{
		int errs = vevo_port_ref_verify( port_ref_ );
		if(errs > 0)
			veejay_msg(0,"%d VEVO ports are still referenced",errs);
		vevo_port_free( port_ref_ );
	}
#endif
}

static	int	vevo_port_get_port( void *port, vevo_storage_t *item, void *res )
{
	if (item->flags & VEVO_PROPERTY_SOFTREF)
		return -1;
	if(item->num_elements == 1 )
		atom_get_value( item, 0, res );
	return item->num_elements;
}

//! Add a Port to the reference port or create a new reference port
/*!
 \param in Input port
 \param ref Reference Port
 \return New port or Input port
 */
void	*vevo_port_register( vevo_port_t *in, vevo_port_t *ref )
{
	void *port = in;
	const void *store = ref;
#ifndef STRICT_CHECKING
	if(!port)
		port = vevo_port_new( VEVO_PORT_POOL );
#else
	if(!port)
		port = vevo_port_new( VEVO_PORT_POOL, __FUNCTION__, __LINE__ );
#endif
	char pkey[32];

#ifdef STRICT_CHECKING
	assert( vevo_port_verify( port ) == 1 );
#endif
	
	if(store)
	{
		sprintf(pkey,"%p", ref);
		vevo_property_set( port, pkey,VEVO_ATOM_TYPE_PORTPTR, 1 , &ref );
	}
	return port;
}

//! Merge 2 Ports, A + B = AB. Only for properties that have atom VOIDPTR
/**! all in port_a is added to port_b
 \param port Port A
 \param port Port B
 \return Error code
 */
int	vevo_union_ports( void *port_a, void *port_b, int filter_type )
{
   __vevo_port_t *A = (__vevo_port_t *) port_a;
   __vevo_port_t *B = (__vevo_port_t *) port_b;

   if(!A || !B )
  	return 1;

   char **Ea = vevo_list_properties( port_a );
   int i;
   int error;
   for( i = 0; Ea[i] != NULL; i ++ )
   {
	if(!vevo_property_exists( port_b, Ea[i] )&& vevo_property_atom_type( port_a, Ea[i] ) == 
	 	filter_type )
	{
#ifdef STRICT_CHECKING
		int n = vevo_property_num_elements( port_b, Ea[i] );
		// clone elements!
		assert( n <= 1 );
#endif
		void *v = NULL;
		error = vevo_property_get( port_a, Ea[i], 0, &v );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		error = vevo_property_set( port_b, Ea[i], 
				VEVO_ATOM_TYPE_VOIDPTR,1, &v );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
	}
	free( Ea[i] );	
   }
   free( Ea );

   return 0;
}

//! Adds all elements in port_b of type VOIDPTR to port_a. Key value tupple is reversed. 
/*! 
 * \param port Port A
 \param port Port B
 \return Error code
 */

int	vevo_special_union_ports( void *port_a, void *port_b )
{
   __vevo_port_t *A = (__vevo_port_t *) port_a;
   __vevo_port_t *B = (__vevo_port_t *) port_b;

   if(!A || !B )
  	return 0;

	// port_a contains only slots
   
   char **Ea = vevo_list_properties( port_a );
   int i;
   int error;
   if(!Ea)
	   return 0;
  
   for( i = 0; Ea[i] != NULL; i ++ )
   {
	void *value = NULL;
	char  key[64];

	int	atom_type = vevo_property_atom_type( port_a, Ea[i] );
	if( atom_type == VEVO_ATOM_TYPE_VOIDPTR )
	{
		error = vevo_property_get( port_a, Ea[i], 0, &value );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
	 	sprintf(key, "%p", value );

		if(!vevo_property_exists( port_b, key ))
		{
			error = vevo_property_set( port_b, key, VEVO_ATOM_TYPE_VOIDPTR,1,&value );
#ifdef STRICT_CHECKING
		assert( error == VEVO_NO_ERROR );
#endif
		}
	}
	free( Ea[i] );	
   }
   free( Ea );
   return 0;
}
//! Recursivly destroy a Port and all sub ports
/**!
 \param port Port to destroy
 */
void	vevo_port_recursive_free( vevo_port_t *port )
{
	if(!port)
		return;
	void *sor = vevo_port_register( NULL,NULL );
#ifdef STRICT_CHECKING
	assert( sor != NULL );
#endif
	vevo_port_recurse_free( sor, port );
	#ifdef STRICT_CHECKING
	assert( sor != NULL );
#endif
	int i;
	vevo_storage_t **item = vevo_list_nodes_( sor, VEVO_ATOM_TYPE_PORTPTR );
	if(!item)
	{
		vevo_port_free(sor);
#ifdef STRICT_CHECKING
		assert( 0 );
#endif
		return;
	}
#ifdef STRICT_CHECKING
	assert( item != NULL );
	int n_freed = 0;
#endif
	for( i = 0; item[i] != NULL ; i ++ )
	{
		void *sub_port = NULL;
	
		int ec = atom_get_value( item[i],0,&sub_port );
		if( ec == VEVO_NO_ERROR )
		{
#ifdef STRICT_CHECKING
			assert(sub_port != NULL);
			n_freed++;
#endif
			vevo_port_free( sub_port );
		}
	}
	free(item);	
	vevo_port_free( sor );
}


char	**vevo_port_deepen_namespace( void *port, char *path)
{
#ifdef STRICT_CHECKING
	assert( port != NULL );
	assert( path != NULL );
#endif
	char **top_level = vevo_list_properties(port);
	int k;
	int num = 0;
	if(!top_level)
	{
		return 0;
	}
	num = vevo_num_properties(port );

	if( num == 0 )
	{
		return NULL;
	}
	char **res = (char**) malloc( sizeof(char*) * (num+1));
	memset(res,0,sizeof(char*) * (num+1));
	int n = 0;
	
	for( k = 0; top_level[k] != NULL ; k ++ )
	{
		int atom_type = vevo_property_atom_type( port, top_level[k] );
		if( atom_type != VEVO_ATOM_TYPE_VOIDPTR && atom_type != VEVO_ATOM_TYPE_PORTPTR )
		{
			int len = strlen(path) + 2;
			len += strlen( top_level[k] );
			res[n] = (char*) malloc(sizeof(char) * len );
			snprintf(res[n],len,"%s/%s",path,top_level[k]);
			n++;
		}
		free(top_level[k]);	
	}
	free(top_level);
	res[n] = NULL;
	return res;
}

char	**vevo_port_recurse_namespace( vevo_port_t *port, const char *base )

{

	char **top_level = vevo_list_properties(port);
	if(!top_level)
		return NULL;
	int k;
	int num = 0;
	for( k = 0; top_level[k] != NULL ; k ++ )
	{
		int atom_type = vevo_property_atom_type( port, top_level[k] );
		if( atom_type == VEVO_ATOM_TYPE_PORTPTR )
		{
			void *p = NULL;
			int error = vevo_property_get( port, top_level[k],0,&p );
			if( error == VEVO_NO_ERROR )
			{
#ifdef STRICT_CHECKING
				assert( p != NULL );
#endif
				num += vevo_num_properties(p);
			}
		}	
		else if (atom_type != VEVO_ATOM_TYPE_VOIDPTR)
			num++;
	}
	
#ifdef STRICT_CHECKING
	assert( num > 0 );
#endif
	
	char **namespace = (char**) vj_malloc(sizeof(char*) * (num+1) );
#ifdef STRICT_CHECKING
	assert( namespace != NULL );
#endif
	memset(namespace, 0,sizeof(char*) * (num+1));

	int    methods = 0;
	int	error;
	for( k = 0; top_level[k] != NULL ; k ++ )
	{
#ifdef STRICT_CHECKING
		assert( methods <= num );
#endif
		int atom_type = vevo_property_atom_type( port, top_level[k] );
		char path[256];

		sprintf(path, "%s", base);
		
		if( atom_type == VEVO_ATOM_TYPE_PORTPTR )
		{
			void *exp = NULL;
			error = vevo_property_get( port, top_level[k],0,&exp );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif	
			strcat( path, "/" );
			strcat( path, top_level[k] );

			char **ns = vevo_port_deepen_namespace( exp, path );
			if( ns )
			{
			int  j;
			for( j = 0; ns[j] != NULL ; j ++ )
			{
				namespace[methods] = strdup( ns[j]);
				methods++;
				free(ns[j]);
			}
			free(ns);
			}
		}
		else if( atom_type != VEVO_ATOM_TYPE_VOIDPTR )
		{
			strcat( path, "/" );
			strcat( path, top_level[k] );
			namespace[ methods ] = strdup( path );
			methods ++;
		}
		free(top_level[k]);
	}
	free(top_level);
	return namespace;		
}
//! Flatten all ports and return list of ports to be destroyed
/*!
 \param sorted_port Reference Port
 \param p Top level Port to scan
 */
static void	vevo_port_recurse_free( vevo_port_t *sorted_port, vevo_port_t *p )
{
#ifdef STRICT_CHECKING
	assert( p != NULL );
#endif
	vevo_storage_t **item = vevo_list_nodes_( p, VEVO_ATOM_TYPE_PORTPTR );
	if(!item)
	{
		free(item);
		return;
	}

	vevo_port_register( sorted_port, p );
	int i;
	for( i = 0; item[i] != NULL ; i ++ )
	{
		void *q = NULL;
		int n = vevo_port_get_port( p, item[i], &q );	

		if( n == 1 && q != NULL )
		{	
			if(!vevo_scan_for_atom( q, VEVO_ATOM_TYPE_PORTPTR ))
				vevo_port_register( sorted_port,q );
			else
				vevo_port_recurse_free( sorted_port,q );
		}
		else
		{
			if( n > 1 )
			{
				int k = 0;
				for( k = 0; k < item[i]->num_elements; k ++ )
				{
					void *qq = NULL;
					atom_get_value( item[i], k, &qq );
					if(!vevo_scan_for_atom(qq,VEVO_ATOM_TYPE_PORTPTR ))
						vevo_port_register( sorted_port,qq );
					else
						vevo_port_recurse_free( sorted_port,qq );
				}
			}
		}

	}
	free(item);
}

//! Run over port reference port and verify that all ports have been freed
/*!
 \param p pointer to reference port
 \return error code
*/

static int	vevo_port_ref_verify( vevo_port_t *p) 
{
	int err = 0;
#ifdef STRICT_CHECKING
	char **item = NULL;
	int i;
	int ref_count = 0;
	item = vevo_list_properties( p );
	if( item == NULL )
	{
		veejay_msg(0, "%s: No properties in %p",p);
		return 1;
	}
	for( i = 0; item[i] != NULL ; i ++ )
	{
		int error = vevo_property_get( port_ref_,item[i],0,&ref_count);

		if( error == VEVO_ERROR_PROPERTY_EMPTY )
			ref_count = 0;
		else
		{
			if( error != VEVO_NO_ERROR )
			{
				veejay_msg(0, "Port '%p' reference unexpected error %d)", item[i],error );
				err++;
			}
		}
		
		if( ref_count != 0 )
		{
			void *which_port = NULL;
			error = vevo_property_get( port_ref_, item[i],0, &which_port );
			if( error )
				veejay_msg(0, "Port '%s' retrieval returns error %d", item[i],error );
//			else
//				veejay_msg(0, "Reference to Port %s exists, but may be invalid", item[i]);
//			veejay_msg(0, "Port '%s' reference count is %d",item[i], ref_count);
			err++;
		}
		ref_count = 1;
		free(item[i]);
	}
	free(item);
#endif
	return err;
}
static	char	*vevo_property_get_str( vevo_port_t *port, const char *key )
{
  	size_t len = vevo_property_element_size( port, key, 0 );
        char *ret = NULL;
        if(len<=0) return NULL;
        ret = (char*) malloc(sizeof(char) * len );
        vevo_property_get( port, key, 0, &ret );
        return ret;
}

static	char	*vevo_format_inline_property( vevo_port_t *port, int n_elem, int type )
{
	char *res = NULL;
	char token[5];
	bzero(token,5);
	switch(type)
	{
		case VEVO_ATOM_TYPE_INT:
			token[0] = 'd';
			break;
		case VEVO_ATOM_TYPE_BOOL:
			token[0] = 'b';
			break;
		case VEVO_ATOM_TYPE_UINT64:
			token[0] = 'D';
			break;
		case VEVO_ATOM_TYPE_STRING:
			token[0] = 's';
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			token[0] = 'g';
			break;

	}
		
	if( token[0])
	{
		int len = n_elem * strlen(token) + 1;
		res = (char*) calloc(1, sizeof(char) * len );
		int i;
		for( i =0; i < n_elem; i ++ )
			strncat( res,token,strlen(token) );
	}
	return res;
}

char	*vevo_format_property( vevo_port_t *port, const char *key )
{
	char *res = NULL;
	char token[5];
	int	atom_type = vevo_property_atom_type( port, key );

	int	n_elem    = vevo_property_num_elements( port, key );
	
	if( n_elem <= 0 )
		n_elem = 1;

	bzero(token,5);
	
	switch( atom_type )
	{
		case VEVO_ATOM_TYPE_INT:
		case VEVO_ATOM_TYPE_BOOL:
			token[0] = 'd';
		       break;
	        case VEVO_ATOM_TYPE_UINT64:
			token[0] = 'D';
	 	       break;
       	 	case VEVO_ATOM_TYPE_DOUBLE:
			token[0] = 'g';
 			break;
		case VEVO_ATOM_TYPE_STRING:
			token[0] = 's';
			break;
		case VEVO_ATOM_TYPE_VOIDPTR:
			token[0] = 'x';
			break;
		case VEVO_ATOM_TYPE_PORTPTR:
			token[0] = 'p';
			break;	
		default:
			token[0] = 'g';
			break;		
	}
	
	if( token[0])
	{
		int len = n_elem * strlen(token) + 1;
		res = (char*) calloc(1, sizeof(char) * len );
		int i;
		for( i =0; i < n_elem; i ++ )
			strncat( res,token,strlen(token) );
	}
	
	return res;

}
char	*vevo_format_kind( vevo_port_t *port, const char *key )
{
	char *res = NULL;
	char token[5];
	int	atom_type = vevo_property_atom_type( port, key );

	bzero(token,5);
	
	switch( atom_type )
	{
		case VEVO_ATOM_TYPE_INT:
		case VEVO_ATOM_TYPE_BOOL:
			token[0] = 'd';
		       break;
	        case VEVO_ATOM_TYPE_UINT64:
			token[0] = 'D';
	 	       break;
       	 	case VEVO_ATOM_TYPE_DOUBLE:
			token[0] = 'g';
 			break;
		case VEVO_ATOM_TYPE_STRING:
			token[0] = 's';
			break;
		case VEVO_ATOM_TYPE_VOIDPTR:
			token[0] = 'x';
			break;
		case VEVO_ATOM_TYPE_PORTPTR:
			token[0] = 'p';
			break;
		default:
			token[0] = 'g';
			break;		
	}
	
	if( token[0])
	{
		res = strdup( token );
	}
	
	return res;

}


static const char *vevo_split_token_( const char *s, const char delim, char *buf, int buf_len )
{
	const char *c = s;
	int n = 0;
	while(*c && n < buf_len)
	{
		*c++;
		n++;

		if( *c == delim )
		{
			strncpy( buf,s,n );
			return c+1;
		}
	}
	return NULL;
}

static char *vevo_scan_token_( const char *s )
{
	const char *c = s;
	int   n = 0;
	int   ld = 0;
	int   fk = 0;
	while( *c )
	{
		if(*c == ':')
			ld = n + 1;
		if(*c == '=' && ld > 0)
		{
			fk = 1;
			break;
		}
		*c++;
		n++;
	}
	char *res = NULL;
	
	if( ld > 0 && fk )
		res = strndup( s, ld );

	return res;
}

static const char *vevo_split_token_q( const char *s, const char delim, char *buf, int buf_len )
{
	const char *c = s;
	int n = 0;

	if( *c != '"' )
		return NULL;
	
	while(*c && n < buf_len)
	{
		*c++;
		n++;

		if( *c == delim && n > 2)
		{
			strncpy( buf,s+1,n-1 );
			return c;
		}
	}
	return NULL;
}

//! Write all keys and values in character string to a port'
/*!
 \param port Port
 \param s Character string
 \return Error Code 
 */
int	vevo_sscanf_port( vevo_port_t *port, const char *s )
{

	const char *ptr = s;
	int   len = strlen(s);
	int   i = 0;
	while( len > 0 )
	{
		char *token = vevo_scan_token_(ptr);
		int token_len;
		if( token )
		{
			token_len = strlen(token);
			if(vevo_sscanf_property( port, token ))
				i++;
		}
		else
		{
			token_len = len;
			if(vevo_sscanf_property( port, ptr ))
				i++;
		}
		len -= token_len;
		ptr += token_len;
	}
	return i;	
}
//! Read a key and value from a character string into a Port'
/*!
 \param port Port
 \param s character string
 \return error code
 */
#define	MAX_ELEMENTS 64
int	vevo_sscanf_property( vevo_port_t *port, const char *s)
{
	int done = 0;
	char key[PROPERTY_KEY_SIZE];
	bzero(key, PROPERTY_KEY_SIZE );	
	const char *value = vevo_split_token_(s, '=', key, PROPERTY_KEY_SIZE );
	if(value==NULL)
		return 0;

	char *format = vevo_format_property( port, key );
	int  atom    = vevo_property_atom_type( port, key );

	if( format == NULL )
		return done;
	if(atom==-1)
		atom = VEVO_ATOM_TYPE_DOUBLE;
	//@ if a property does not exist, DOUBLE is assumed
	//@ DOUBLE is valid for all sample's of type capture.
	
	uint64_t i64_val[MAX_ELEMENTS];
	int32_t	 i32_val[MAX_ELEMENTS];
	double   dbl_val[MAX_ELEMENTS];
	char     *str_val[MAX_ELEMENTS];
	
	int	 cur_elem = 0;
	int	 n = 0;
	
	const char 	*p = value;
	char	*fmt = format;
	while( *fmt != '\0' )
	{
		char arg[256];
		bzero(arg,256);
		
		if( *fmt == 's' )
			p = vevo_split_token_q( p, ':', arg, 1024 );
		else
			p = vevo_split_token_( p, ':', arg, 1024 );

		if( p == NULL )
			return 0;
		
		if( arg[0] != ':' ) 
		{
			switch(*fmt)
			{
				case 'd':
					n = sscanf( arg, "%d", &(i32_val[cur_elem]));
					break;
				case 'D':
					n = sscanf( arg, "%lld", &(i64_val[cur_elem]));
					break;
				case 'g':
					n = sscanf( arg, "%lf", &(dbl_val[cur_elem]));
					break;
				case 's':
					str_val[cur_elem] = strdup( arg );
					n = 1;
					break;
				default:
					n = 0;
					break;
			}
		}
		else
		{
			n = 0;
		}
		
		*fmt++;
		cur_elem ++;
	}

	void *ptr = NULL;
	if( n > 0 )
	switch( *format )
	{
		case 'd':
			ptr = &(i32_val[0]);
			break;
		case 'D':
			ptr = &(i64_val[0]);
			break;
		case 'g':
			ptr = &(dbl_val[0]);
			break;
		case 's':
			ptr = &(str_val[0]);
			break;
	}	
	
	int error = 0;

	//veejay_msg(0, "Set: '%s' : %d, %g", key,n, dbl_val[0] );
	
	if( n == 0 )
		error = vevo_property_set( port, key, atom, 0, NULL );
	else
		error = vevo_property_set( port, key, atom, cur_elem, ptr );

	if( error == VEVO_NO_ERROR )
		done = 1;
	return done;
}

//! Write all properties in a port to a new character string in the form 'key=[value:value:value]:'
/*!
 \param port Port
 \return A new character string 
 */
char	**vevo_sprintf_port( vevo_port_t *port )
{
	int    i;
	int    k = 0;
	int    num  = vevo_num_properties(port);
	if( num == 0 )
		return NULL;

	char  **keys = vevo_list_properties(port );

	char **res  = (char**) calloc(1, sizeof(char*) * (num+1) );

	for( i = 0; keys[i] != NULL; i ++ )
	{
		char *buf = vevo_sprintf_property(port, keys[i]);
		char *p = buf;
		if(buf)
		{	
			res[k++] = strdup( buf );
			free(p);
		}
		free(keys[i]);
		
	}
	res[num] = NULL;
	
	free(keys);
	
	return res;
}
//! Write property value to a new character string in the form 'key=[value:value:value]:'
/*!
 \param port Port
 \param key Property to write
 \return A new character string 
 */

char	*vevo_sprintf_property_value( vevo_port_t *port, const char *key)
{
	char tmp[512];
	int n = vevo_property_num_elements( port, key );
	if( n <= 0 )
		return NULL;
	int i;
	int size = 0;
	int atom = vevo_property_atom_type( port , key );

	bzero(tmp,512);
	
	if(atom == VEVO_ATOM_TYPE_INT || atom == VEVO_ATOM_TYPE_BOOL)
	{
		int *a = (int*) malloc(sizeof(int) * n );
		for( i = 0; i < n ; i ++ )
			if( vevo_property_get( port, key, i, &(a[i]) ) != VEVO_NO_ERROR)
			{
				free(a);
				return NULL;
			}
			else
			{
				char val[16];
				sprintf(val, "%4d", a[i]);
				strcat( tmp, val );
				strcat( tmp, " ");
			}
		free(a);
	} else if (atom == VEVO_ATOM_TYPE_DOUBLE )
	{
		double *a = (double*) malloc(sizeof(double) * n );
		for( i = 0; i < n; i ++ )
			if( vevo_property_get( port, key, i , &(a[i])) != VEVO_NO_ERROR)
			{
				free(a);
				return NULL;
			}
			else
			{
				char val[16];
				sprintf(val, "%04.2g", a[i]);
				strcat( tmp, val );
				strcat( tmp, " " );
			}
		free(a);
	} else
		return NULL;
	
	return strdup( tmp );
}


#define PROP_MAX_LEN 1024
#define PROP_ARG_LEN 256
char  *vevo_sprintf_property( vevo_port_t *port, const char *key  )
{
	char *format = vevo_format_property( port, key );
	if( format == NULL )
		return NULL;
	char *res = (char*) calloc( 1, sizeof(char) * PROP_MAX_LEN );
	int  n_elems = 0;

	int32_t i32_val = 0;
	uint64_t i64_val = 0;
	double dbl_val = 0.0;
	char	*str_val = NULL;	
	int	error = 0;
	int 	nerr  = 0;
	int	size = PROP_MAX_LEN;	

	void	*vport = NULL;
	
	sprintf(res, "%s=", key );
	
	while( *format && nerr == 0)
	{
		char 	tmp[1024];
		bzero(tmp,256);
		switch(*format)
		{
			case 'd':
				error = vevo_property_get(port,key,n_elems,&i32_val);
				if( error == VEVO_NO_ERROR ) {
					sprintf(tmp, "%d:", i32_val );
				} else if (error == VEVO_ERROR_PROPERTY_EMPTY ) {
					tmp[0] = ':';
				} else
					nerr ++;
				break;
			case 'D':
				error = vevo_property_get(port,key,n_elems,&i64_val);
				if( error == VEVO_NO_ERROR ) {
					sprintf(tmp, "%lld:", i64_val );
				} else if( error == VEVO_ERROR_PROPERTY_EMPTY ) {
					tmp[0] = ':';
				} else
					nerr ++;
				break;
			case 'g':
				error = vevo_property_get(port,key,n_elems,&dbl_val);
				if( error == VEVO_NO_ERROR ) {
					sprintf(tmp, "%g:", dbl_val );
				} else if ( error == VEVO_ERROR_PROPERTY_EMPTY ) {
					tmp[0] = ':';
				} else
					nerr ++;
				break;
			case 's':
				str_val = vevo_property_get_str( port, key );
				if(str_val)
				{
					tmp[0] = '\"';
					strncat(tmp+1,str_val,250);
					int n = strlen(tmp);
					tmp[n] = '\"';
					tmp[n+1] = ':';
				}
				else
				{
					tmp[0] = '\"';
					tmp[1] = '\"';
					tmp[2] = ':';
				}
				str_val = NULL;
				break;
			case 'x':
				break;
			case 'p':
				{
					int num = 0;
					if(n_elems == 0 )
					{
						error = vevo_property_get(port,key,0,&vport );
						if(error == VEVO_NO_ERROR )
						num  = vevo_num_properties(vport);
					}

					if( num > 0 )
					{
						char **pstr = vevo_sprintf_port( vport );
						if(pstr)
						{
							int k;

							sprintf(tmp, "[%s",key);	
							
						        for( k = 0; pstr[k] != NULL; k ++ )
						        {
							       strncat(tmp, pstr[k], strlen(pstr[k]));
							       free(pstr[k]);
						        }
							free(pstr);

							int n = strlen(tmp);
							tmp[n] =']';
							tmp[n+1] = ':';
						}
					}
				}
				break;
		}
		*format++;
		n_elems++;
		
		if( nerr )
			break;

		size -= strlen(tmp);
		if(size > 0)		
			strcat(res, tmp  );
		else
			nerr++;
	}

	if( nerr )
	{
		if( res ) free(res);
		res = NULL;
	}
	return res;
}
int	vevo_property_from_string( vevo_port_t *port, const char *s, const char *key, int n_elem, int type)
{
	int done = 0;
	char *format = vevo_format_inline_property( port, n_elem, type );
	
	if( format == NULL )
		return done;
	
	uint64_t i64_val[MAX_ELEMENTS];
	int32_t	 i32_val[MAX_ELEMENTS];
	double   dbl_val[MAX_ELEMENTS];
	char     *str_val[MAX_ELEMENTS];
	
	int	 cur_elem = 0;
	int	 n = 0;
	const char 	*p = s;
	char	*fmt = format;
	while( *fmt != '\0' )
	{
		char arg[256];
		bzero(arg,256);
		
		if( *fmt == 's' )
			p = vevo_split_token_q( p, ':', arg, 1024 );
		else
			p = vevo_split_token_( p, ':', arg, 1024 );

		if( p == NULL )
		{
			veejay_msg(0,"Invalid value. Use 'value:' ");
			free(format);
			return 0;
		}
		if( arg[0] != ':' ) 
		{
			switch(*fmt)
			{
				case 'd':
				case 'b':
					n = sscanf( arg, "%d", &(i32_val[cur_elem]));
					break;
				case 'D':
					n = sscanf( arg, "%lld", &(i64_val[cur_elem]));
#ifdef STRICT_CHECKING
					assert( n == 1 );
#endif
					break;
				case 'g':
					n = sscanf( arg, "%lf", &(dbl_val[cur_elem]));
					break;
				case 's':
					str_val[cur_elem] = strdup( arg );
					n = 1;
					break;
				default:
					n = 0;
					break;
			}
		}
		else
		{
			n = 0;
		}
		
		*fmt++;
		cur_elem ++;
	}

	void *ptr = NULL;
	if( n > 0 )
	switch( *format )
	{
		case 'd':
		case 'b':
			ptr = &(i32_val[0]);
			break;
		case 'D':
			ptr = &(i64_val[0]);
			break;
		case 'g':
			ptr = &(dbl_val[0]);
			break;
		case 's':
			ptr = &(str_val[0]);
			break;
	}	
	
	int error = 0;
	if( n == 0 )
		error = vevo_property_set( port, key, type, 0, NULL );
	else
	{
#ifdef STRICT_CHECKING
		assert( port != NULL );
		assert( ptr != NULL );
#endif
		error = vevo_property_set( port, key, type, cur_elem, ptr );
	}
	if( error == VEVO_NO_ERROR )
		done = 1;
	free(format);
	return done;
}

char            *vevo_property_get_string( void *port, const char *key )
{
        size_t len = vevo_property_element_size( port, key, 0 );
        char *ret = NULL;
        if(len<=0) return NULL;

	if( vevo_property_get( port, key,0,NULL ) != VEVO_NO_ERROR )
		return NULL;
	
#ifdef STRICT_CHECKING
	assert( vevo_property_atom_type( port, key ) == VEVO_ATOM_TYPE_STRING );
#endif
        ret = (char*) vj_malloc(sizeof(char) * len );
        vevo_property_get( port, key, 0, &ret );
        return ret;
}


int
vevo_property_del(vevo_port_t * p,
		    const char *key )
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
	//@ no self referencing
    assert( key != NULL );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    uint64_t hash_key = hash_key_code(key);
    void *node = NULL;
    if (!port->table) {
	    veejay_msg(0, "Is LIst");
	vevo_property_t *pnode = NULL;
	if ((pnode = prop_node_get(port, hash_key)) != NULL) {
	    vevo_free_storage(port,pnode->st);
	    prop_node_free(port,pnode);
	    //node = (void *) pnode;
	}
    } else {
	    veejay_msg(0, "Is Hash");
	hnode_t *old_node = NULL;
	if ((old_node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *oldstor =
		(vevo_storage_t *) hnode_get(old_node);
	    vevo_free_storage(port,oldstor);
	    hash_delete((hash_t *) port->table, old_node);
	    hnode_destroy(old_node);
	}
    }

    port_index_t *l = port->index;
    port_index_t *n = NULL;
     while (l != NULL) {
	n = l->next;
	if( l->key == hash_key )
		port_node_free(port,l);
	l = n;
     }

    return VEVO_NO_ERROR;
}

