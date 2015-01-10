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
 *        -# gchar*  VEVO_ATOM_TYPE_UTF8STRING
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
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <libvevo/vevo.h>

#include <libvevo/libvevo.h>
#include <libhash/hash.h>
#include <libvjmem/vjmem.h>
#include <libvjmsg/vj-msg.h>
//#include <glib-2.0/glib.h> //@ for g_locale_to_utf8 and g_locale_from_utf8
#ifdef STRICT_CHECKING
#include <assert.h>
#define VEVO_DBG
#endif

//#ifdef VEVO_DBG
//#define VVERBOSE 1
//#endif

#include	<libvevo/pool.h>
#define PORT_TYPE_PLUGIN_INFO 1
#define PORT_TYPE_FILTER_CLASS 2
#define PORT_TYPE_FILTER_INSTANCE 3
#define PORT_TYPE_CHANNEL_TEMPLATE 4
#define PORT_TYPE_PARAMETER_TEMPLATE 5
#define PORT_TYPE_CHANNEL 6
#define PORT_TYPE_PARAMETER 7
#define PORT_TYPE_GUI 8
/*
 * uncomment to track port new/free calls
 * requires -DSTRICT_CHECKING
 */


//#ifdef STRICT_CHECKING
//#define VEVO_TRACKPORTS
//#endif

//

//! \typedef atom_t
/*! \brief atom
 * 
 *  the atom_t structure
 */


#ifdef ARCH_X86_64
typedef uint64_t ukey_t;
#else
typedef uint32_t ukey_t;
#endif

typedef	void (*vevo_set_func)(void *ctx, int32_t type, int32_t value );
typedef int (*vevo_get_func)(void *ctx );

typedef struct {
    int type;
   	void *value;
	int	(*get_func)();
	void(*set_func)();
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
    ukey_t key;
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
    ukey_t hash_code;		/* vevo uses a integer representation of key, eliminates strcmp  */
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
/*#ifdef STRICT_CHECKING
static	vevo_port_t	*port_ref_ = NULL;
#endif*/
static  size_t		atom_sizes_[100];

#ifdef VEVO_TRACKPORTS
static void *trackports = NULL;
static int hastrackports = 0;
#endif

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
static vevo_property_t *prop_node_new(__vevo_port_t *port, ukey_t hash_key,
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
static vevo_property_t *prop_node_append(vevo_port_t * p, ukey_t key,
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
static vevo_property_t *prop_node_get(vevo_port_t * p, ukey_t key)
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
static port_index_t *port_node_new(__vevo_port_t *port,const char *key, ukey_t hash_key)
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
	free((void*)node->key);
	node->key=NULL;
	vevo_pool_free_node( port->pool,(void*)node );
    node = NULL;
}

//! Add a mnemonic to the list of key | hash value pairs
/*!
 \param p port
 \param key name of property
 \param hash_key calculated hash value
 */
static void port_node_append(vevo_port_t * p, const char *key,
				    ukey_t hash_key)
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
//#define property_exists( port, key ) hash_lookup( (hash_t*) port->table, (const void*) key )
static inline hnode_t *property_exists( __vevo_port_t *port, ukey_t key )
{
	return (hnode_t*) hash_lookup( (hash_t*) port->table,(const void*) key );
}

//! \define atom_store__ store atom
#define atom_store__(value) {\
for (i = 0; i < d->num_elements; i++)\
 d->elements.array[i] = vevo_put_atom(port, &value[i], v ); }


#define atom_store_at__(value,at) {\
	vevo_free_atom(port, d->elements.array[at]);\
	d->elements.array[at] = vevo_put_atom(port, value, v ); }

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

static void
storage_put_atom_value(__vevo_port_t * port, void *src, int n,
		       vevo_storage_t * d, int v);

//! Calculate a hash value from a given string
/*!
 \param str string
 \return calculated hash value
 */
static inline ukey_t hash_key_code( const char *str )   	
{
        ukey_t hash = 5381;
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
    ukey_t hash_key = hash_key_code(key);
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

#ifdef STRICT_CHECKING
//! Flag a property as read-only. Use to avoid overriding the value
/*!
 \param p port
 \param key property to set as readonly
 \return error code
 */

static int vevo_property_finalize(vevo_port_t * p, const char *key)
{
    __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);
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
	  //  hnode_t *new_node = hnode_create((void *) stor);
	  //  hnode_put(new_node, (void *) hash_key);
	  //  hnode_destroy(new_node);
	    return VEVO_NO_ERROR;
	}
    }
    return VEVO_ERROR_NOSUCH_PROPERTY;
}
#endif

int	vevo_property_protect( vevo_port_t *p, const char *key  )
{
 __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);
    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	    node->st->flags |= VEVO_PROPERTY_PROTECTED;
	    return VEVO_NO_ERROR;
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
	    stor->flags |= VEVO_PROPERTY_PROTECTED;
	    return VEVO_NO_ERROR;
	}
    }
    return VEVO_ERROR_NOSUCH_PROPERTY;
}

int	vevo_property_softref( vevo_port_t *p, const char *key  )
{
 __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);
    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	    node->st->flags |= VEVO_PROPERTY_SOFTREF;
	    return VEVO_NO_ERROR;
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
	    stor->flags |= VEVO_PROPERTY_SOFTREF;
	    return VEVO_NO_ERROR;
	}
    }
    return VEVO_ERROR_NOSUCH_PROPERTY;
}


static int vevo_property_exists( vevo_port_t *p, const char *key)
{
	__vevo_port_t *port = (__vevo_port_t *) p;

    	ukey_t hash_key = hash_key_code(key);
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

	ukey_t hash_key = hash_key_code(key);
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
/*
#ifdef STRICT_CHECKING
	if( port_type != VEVO_PORT_REFERENCES )
	{
		int ref = 1;
		char ref_key[64];
		sprintf(ref_key,"%p",port );
		vevo_property_set( port_ref_, ref_key, VEVO_ATOM_TYPE_INT, 1, &ref );
	}
#endif 
*/
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
  	//	return VEVO_ERROR_PROPERTY_EMPTY;

    if( dst == NULL )
	    return VEVO_NO_ERROR;
   
    if( t->atom_type == VEVO_ATOM_TYPE_STRING || t->atom_type == VEVO_ATOM_TYPE_UTF8STRING ) {
	char **ptr = (char **) dst;
	char *p = *ptr;
#ifdef STRICT_CHECKING
	assert( atom->size > 0 );
#endif
	veejay_memcpy(p, atom->value, atom->size);
    } else if( t->atom_type != VEVO_ATOM_TYPE_FUNCPTR ) {
#ifdef STRICT_CHECKING
	assert( atom->size > 0 );
#endif
	veejay_memcpy(dst, atom->value, atom->size);
    } else {
#ifdef STRICT_CHECKING
		veejay_msg(VEEJAY_MSG_WARNING, "vevo_property_get called on getter function ?!");
#endif
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

	if( atom_size == 0 )
		return atom;

    if(atom_type == VEVO_ATOM_TYPE_STRING || atom_type == VEVO_ATOM_TYPE_UTF8STRING )
	{
   		atom->value = (atom_size > 0 ?(void*)vj_malloc(atom_size):NULL);//@ FIXME: strings do not come from pool	
	}else
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
			case VEVO_ATOM_TYPE_UTF8STRING:
				free( atom->value );
				break;
		}
		atom->value = NULL;
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

    if (atom_type == VEVO_ATOM_TYPE_STRING || atom_type == VEVO_ATOM_TYPE_UTF8STRING) {
	char **s = (char **) dst;
	char *data = (char*) *s;
	atom_size = strlen(data) + 1;
	atom = vevo_new_atom(port, atom_type, atom_size);
		if (atom_size > 0) {
			veejay_memcpy(atom->value, data, atom_size);
		}
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
	veejay_memcpy(atom->value, dst, atom_size);
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


	//@ overwrite 
    if (d->num_elements == 0 || d->num_elements == 1) {
		//@ single atoms
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
			{
#ifdef STRICT_CHECKING
				assert( d->elements.array[i] != NULL );
#endif
				vevo_free_atom(port,d->elements.array[i]);
			}
			free(d->elements.array);
	    }
    }
#ifdef STRICT_CHECKING
	else {
		assert( d->num_elements >= 0 );
	}
#endif

#ifdef STRICT_CHECKING
	if( d->num_elements > 1 && n > 0 )
		assert( n == d->num_elements );
#endif

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
		d->elements.array = (atom_t **) vj_malloc(sizeof(atom_t *) * n);
		if (d->atom_type == VEVO_ATOM_TYPE_DOUBLE) {
		    double *value = (double *) src;
	   	 atom_store__(value);
		} else {
		    if (d->atom_type == VEVO_ATOM_TYPE_INT|| d->atom_type == VEVO_ATOM_TYPE_BOOL) {
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

static void
storage_put_atom_func(__vevo_port_t * port, void (*set_func)(),int (*get_func)(), vevo_storage_t * d, int v)
{
    if( set_func == NULL && get_func == NULL )
    {
	 	vevo_free_atom(port,d->elements.atom);
		d->elements.atom = NULL;
    }
  
  	d->atom_type = v;
    d->num_elements = 1;
    d->elements.atom = vevo_new_atom(port, v, 0);
  
	d->elements.atom->set_func = set_func;
	d->elements.atom->get_func = get_func;

  	//memcpy(set_func,d->elements.atom->set_func, sizeof( vevo_set_func ));
	//memcpy(get_func,d->elements.atom->get_func, sizeof( vevo_get_func ));
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
    return ((const ukey_t) key1 == (const ukey_t) key2 ? 0 : 1);
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
    ukey_t hash_key = hash_key_code(key);

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
    ukey_t hash_key = hash_key_code(key);

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


static	int	vevo_storage_size( vevo_storage_t *stor ) {
    int i;
    if( stor->num_elements == 0 ) 
	    return 0;
    if( stor->num_elements == 1 )
	    return stor->elements.atom->size;
    int msize = 0;
    for( i = 0; i < stor->num_elements; i ++ ) {
	msize += stor->elements.array[i]->size;
    }
    return msize;
}

int vevo_property_atom_size(vevo_port_t * p, const char *key)
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert(key != NULL);
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
#ifdef STRICT_CHECKING
    assert(port != NULL);
#endif
    ukey_t hash_key = hash_key_code(key);

    if (!port->table) {
		vevo_property_t *node;
		if ((node = prop_node_get(port, hash_key)) != NULL) {
			return vevo_storage_size( node->st );
    	} 
		else {
		hnode_t *node = NULL;
#ifdef STRICT_CHECKING
		assert(port->table != NULL);
#endif

		if ((node = property_exists(port, hash_key)) != NULL) {
			    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
		    if (stor) {
				return vevo_storage_size( stor );
		    }
    	}

		}
	}
    return -1;
}

#ifdef STRICT_CHECKING
static int vevo_property_is_protected(vevo_port_t * p, const char *key)
{
    __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);

    if (!port->table) {
	vevo_property_t *node;
	if ((node = prop_node_get(port, hash_key)) != NULL)
	    return ( node->st->flags & VEVO_PROPERTY_PROTECTED );
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	    vevo_storage_t *stor = (vevo_storage_t *) hnode_get(node);
	    if (stor)
		return (stor->flags & VEVO_PROPERTY_PROTECTED);
	}
    }
    return VEVO_NO_ERROR;
}
#endif

//! Return size of an Atom at a given index
/*!
 \param p Port
 \param key Property name
 \param idx Index
 \return Byte size of value at Index
 */
size_t
vevo_property_element_size(vevo_port_t * p, const char *key, const int idx )
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert(key != NULL);
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);
#ifdef STRICT_CHECKING
    assert( port );
#endif
    if (!port->table) {
	vevo_property_t *node;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
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
	    }
	   	else {
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
#ifdef VVERBOSE
    veejay_msg( VEEJAY_MSG_INFO, "New port %p (%d) from %s:%d", port,port_type,func,line_num);
#endif
   
#ifdef VEVO_TRACKPORTS
    if( hastrackports == 0 ) {
	    hastrackports = 1;
	    trackports = vpn(1331);
    	veejay_msg(0, "trackport: %p", trackports );
	}

    if( trackports != NULL && port_type != 1331) {
    	char key[64];
		char har[1024];
		char *str = &har[0];
		memset(har,0,sizeof(har));
		snprintf(har,sizeof(har)-1, "%s::%d",func,line_num);
    	snprintf(key,64, "%p", port );
    	assert( vevo_property_set( trackports, key,VEVO_ATOM_TYPE_STRING,1,&str ) == VEVO_NO_ERROR );
    }
#endif
    return (vevo_port_t *) port;
}

#endif

//! Initialize VeVo. Set up bookkeeping information to track Port construction and destruction
void	vevo_strict_init()
{
/*
#ifdef STRICT_CHECKING
	port_ref_ = vevo_port_new( VEVO_PORT_REFERENCES, __FUNCTION__,__LINE__ );
#endif
*/
	memset( atom_sizes_,0,sizeof(atom_sizes_) );
	atom_sizes_[1] = sizeof(int32_t);
	atom_sizes_[2] = sizeof(double);
	atom_sizes_[3] = sizeof(int32_t);
	atom_sizes_[4] = sizeof(char*);
//	atom_sizes_[8] = sizeof(gchar*);
	atom_sizes_[5] = sizeof(uint64_t);
	atom_sizes_[65] = sizeof(void*);
	atom_sizes_[66] = sizeof(vevo_port_t*);
}
//! Destroy a Port
/*!
 \param p (local) Port to destroy
 */
#ifdef STRICT_CHECKING
static int	vevo_port_free_(vevo_port_t *p )
#else
static void vevo_port_free_(vevo_port_t * p)
#endif
{
    __vevo_port_t *port = (__vevo_port_t *) p;
#ifdef STRICT_CHECKING
	int msize = 0;
#endif
#ifdef VEVO_TRACKPORTS
    char key[64];
    snprintf(key,64, "%p", port );
    char *val = vevo_property_get_string( trackports, key );	
    int err = vevo_property_get( trackports, key, 0, NULL );
    if ( err == VEVO_ERROR_PROPERTY_EMPTY ) {
	    	veejay_msg(VEEJAY_MSG_ERROR, "port %s was already freed. (%s)", key,val );
		return;
	}
    else if ( err == VEVO_ERROR_NOSUCH_PROPERTY ) {
		char tkey[64];
		snprintf(tkey, 64, "%p", trackports );
		if(strcasecmp(tkey,key) != 0 ) {
	   	 veejay_msg(VEEJAY_MSG_ERROR, "port %s is not a port (not allocated by vpn). (%s)", key,val);
    	}
	} else if ( err == VEVO_NO_ERROR ) {
		vevo_property_del( trackports, key );
		//	vevo_property_set( trackports, key, VEVO_ATOM_TYPE_PORTPTR,0,NULL );//,VEVO_ATOM_TYPE_STRING, 0, NULL );
    		veejay_msg(VEEJAY_MSG_DEBUG, "port %p freed (%s)",key,val);
	} else {
	    veejay_msg(VEEJAY_MSG_ERROR, "unknown error code %d on port %s. (%s)", err,key,val );
	}
    if(val) free(val);
#endif 

    if (port->table) {
	 if (!hash_isempty((hash_t *) port->table)) {
		hscan_t scan;
		memset( &scan, 0, sizeof(hscan_t));
		hash_scan_begin(&scan, (hash_t *) port->table);
		hnode_t *node = NULL;

		while ((node = hash_scan_next(&scan)) != NULL) {
		    vevo_storage_t *stor = NULL;
		    stor = hnode_get(node);
#ifdef STRICT_CHECKING
		    assert(stor != NULL);
		    assert(node != NULL);
		    assert((const char *) hnode_getkey(node) != NULL);
			msize += vevo_storage_size( stor ); 
#endif
		    vevo_free_storage(port,stor);
		}
		hash_free_nodes((hash_t *) port->table);
	 }
 	 hash_destroy((hash_t *) port->table);
	 port->table = NULL;

    }
    else
    {
	    vevo_property_t *l = port->list;
	    vevo_property_t *n;
	    while (l != NULL) {
		n = l->next;
#ifdef STRICT_CHECKING
		msize += vevo_storage_size( l->st );
#endif
		vevo_free_storage(port,l->st);
		prop_node_free(port,l);
		l = n;
	    }
	    port->list = NULL;
     }

	port_index_t *list = port->index;
	port_index_t *node = list;
	port_index_t *next = NULL;
	while( node != NULL ) {
		next = node->next;
		port_node_free(port, node );
		node = next;
	}
	port->index = NULL;
     vevo_pool_destroy( port->pool );	
     
     free(port);

     p = port = NULL;
#ifdef STRICT_CHECKING
	return msize;
#endif
}

//! Verify if Vevo has allocated a given Port
/*!
 \param port Port to verify
 \return error code
 */
int	vevo_port_verify( vevo_port_t *port )
{/*
#ifdef STRICT_CHECKING
	if( port == port_ref_ )
		return 1;
	char pkey[32];
	sprintf(pkey, "%p",port);

	int ref_count = 0;
	int error =	vevo_property_get( port_ref_, pkey, 0, &ref_count );

	if( error != 0 )
	{
		//veejay_msg(0, "%s: Port '%s' not allocated by vevo_port_new()", __FUNCTION__, pkey );
		return 0;
	}

	if( error == VEVO_ERROR_NOSUCH_ELEMENT || error == 
	 	VEVO_ERROR_NOSUCH_PROPERTY )
	{
		//veejay_msg(0, "%s: Port '%s' does not exist" );
		return 0;
	}
	
	if( ref_count == 0 )
	{
		//veejay_msg(0, "%s: Port %s has a reference count of 0 ",__FUNCTION__, pkey);
		return 0;
	}
#endif
*/
	return 1;
}

//! Destroy a Port
/*!
 \param p Port to destroy
 */
#ifdef STRICT_CHECKING
void	vevo_port_free( vevo_port_t *port, const char *func, const int line_no )
#else
void	vevo_port_free( vevo_port_t *port )
#endif
{
	if(port != NULL) {
		vevo_port_free_(port );
		port = NULL;
	}

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
    ukey_t hash_key = hash_key_code(key);
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
#ifdef VEVO_TRACKPORTS
	veejay_msg(VEEJAY_MSG_DEBUG, "%s: key=%s, type=%d, n=%d, addr=%p",__FUNCTION__,key,atom_type,num_elements,src );
#endif
#ifdef STRICT_CHECKING
    assert(p != NULL);
	//@ no self referencing
    assert( src != p );
    if( num_elements > 0 ) assert( src != NULL );
    assert( key != NULL );
   // if( atom_type != VEVO_ATOM_TYPE_HIDDEN )
  	assert( vevo_atom_size(atom_type) > 0 );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);
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
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO, "Port %p property %s is read-only!",p,key);
#endif
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
	
		if(!port->table) {
	    	node = (void *) prop_node_append(port, hash_key, stor);
    	}
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

		hash_insert((hash_t *) port->table, node2,   (const void *) hash_key);
    }

#ifdef VVERBOSE
	veejay_msg(VEEJAY_MSG_INFO, "Port %p <- set property %s", port, key );
#endif

    return VEVO_NO_ERROR;
}


int
vevo_property_set_f(vevo_port_t * p,
		    const char *key,
		    int atom_type, int num_elements, void (*set_func)(), int (*get_func)() )
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
	//@ no self referencing
    assert( key != NULL );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);
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
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO, "Port %p property %s is read-only!",p,key);
#endif
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
    
    storage_put_atom_func(port, set_func,get_func, stor, atom_type);

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

#ifdef VVERBOSE
	veejay_msg(VEEJAY_MSG_INFO, "Port %p <- set property %s", port, key );
#endif

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
    ukey_t hash_key = hash_key_code(key);

    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> get property %s[%d]",port,key,idx );
#endif
		return atom_get_value(node->st, idx, dst);
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> get property %s[%d]", port,key,idx );
#endif

		return atom_get_value((vevo_storage_t *) hnode_get(node),
				      idx, dst);
	}
    }
#ifdef VVERBOSE
	veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> no such property %s[%d]", port,key,idx );
#endif

    return VEVO_ERROR_NOSUCH_PROPERTY;
}

int
vevo_property_call(vevo_port_t * p, const char *key, void *ctx, int32_t type, int32_t value )
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert( key != NULL );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);

    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> call property %s",port,key );
#endif
		vevo_storage_t *s = node->st;
		vevo_set_func vvfunc = (vevo_set_func) s->elements.atom->set_func;
		vvfunc( ctx,type, value );
		return 1;
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> call property %s", port,key );
#endif
		vevo_storage_t *s = (vevo_storage_t*) hnode_get(node);
		vevo_set_func vvfunc  = (vevo_set_func) s->elements.atom->set_func;
		vvfunc( ctx,type, value );
		return 1;
	}
    }
#ifdef VVERBOSE
	veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> no such property %s", port,key );
#endif

    return VEVO_ERROR_NOSUCH_PROPERTY;
}

int
vevo_property_call_get(vevo_port_t * p, const char *key, void *ctx )
{
#ifdef STRICT_CHECKING
    assert(p != NULL);
    assert( key != NULL );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;
    ukey_t hash_key = hash_key_code(key);

    if (!port->table) {
	vevo_property_t *node = NULL;
	if ((node = prop_node_get(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> call property %s",port,key );
#endif
		vevo_storage_t *s = node->st;
		vevo_get_func vvfunc = (vevo_get_func) s->elements.atom->get_func;
		return vvfunc( ctx );
	}
    } else {
	hnode_t *node = NULL;
	if ((node = property_exists(port, hash_key)) != NULL) {
	   // if (dst == NULL)
	//	return VEVO_NO_ERROR;
	  //  else
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> call property %s", port,key );
#endif
		vevo_storage_t *s = (vevo_storage_t*) hnode_get(node);
		vevo_get_func vvfunc  = (vevo_get_func) s->elements.atom->get_func;
		return vvfunc( ctx );
	}
    }
#ifdef VVERBOSE
	veejay_msg(VEEJAY_MSG_INFO ,"Port %p -> no such property %s", port,key );
#endif

    return 0;
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

    int n = 0;
    int i = 0;
#ifdef STRICT_CHECKING
    assert( port->index != NULL );
#endif

    port_index_t *l = port->index;
    while (l != NULL) {
	n++;
	l = l->next;
#ifdef STRICT_CHECKING
	assert( n > 0 && n < 1048576 );
#endif
    }

	if( n <= 0 )
		return NULL;

    list = (char **) malloc(sizeof(char *) * (n+1) );
    if (!list)
		return NULL;

    l = (port_index_t *) port->index;

#ifdef VVERBOSE
    veejay_msg(VEEJAY_MSG_INFO, "Property list of port %p",port );
#endif

	while (l != NULL) {
#ifdef STRICT_CHECKING
//	if( vevo_property_atom_type( p, l->key ) != VEVO_ATOM_TYPE_HIDDEN )
//	{
#endif
        list[i] = (char *) strdup(l->key);
#ifdef VVERBOSE
		veejay_msg(VEEJAY_MSG_INFO, "\t%s", list[i]);
#endif
		i++;
#ifdef STRICT_CHECKING
//	}
#endif

		l = l->next;
	//i++;
    }

    list[n] = NULL;
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
    
    unsigned int N = 8;	// null terminated list of keys 2 -> 4 -> 8 -> 16 etc
    int idx = 0;

    vevo_storage_t **list = (vevo_storage_t**)malloc(sizeof(vevo_storage_t*) * N );

    if( port->table)
    {
	hnode_t *node = NULL;
	hscan_t scan;
	vevo_storage_t *s = NULL;	
	memset(&scan,0,sizeof(hscan_t));
	hash_scan_begin( &scan,(hash_t*) port->table );
	while((node=hash_scan_next(&scan)) != NULL)
	{
		s = hnode_get(node);
		if (s != NULL && (s->atom_type == atype || atype == 0))
		{
			int type = 0;
			int ec = atom_get_value(s, 0, &type);
			if( ec == VEVO_NO_ERROR && !s->softlink ) {
				list[idx] = s;
				idx ++;
				if( idx >= N ) {
					N *= 2;
					list = (vevo_storage_t**) realloc( list, sizeof(vevo_storage_t*) * N  );
				}
			}
		}
	}
	list[idx] = NULL;
    }
    else
    {
	vevo_property_t *l = port->list;
	vevo_property_t *n = NULL;
	vevo_storage_t *s = NULL;
	while( l != NULL )
	{
		n = l->next;
		s = l->st;
			
		if(s != NULL && (s->atom_type == atype || atype == 0) )
		{
			int type = 0;
			int ec = atom_get_value(l->st, 0, &type);
			if( ec == VEVO_NO_ERROR && !s->softlink ) {
				list[idx] = s;
				idx ++;
				if( idx >= N ) {
					N *= 2;
					list = (vevo_storage_t**) realloc( list, sizeof(vevo_storage_t*) * N  );
				}
			}
		}
		l = n;
	}
	list[idx] = NULL;
    }
    return list;
}

//! Report statistics and free bookkeeping information
void	vevo_report_stats()
{
#ifdef VEVO_TRACKPORTS
	vevo_port_dump( trackports,0);
	char **items = vevo_list_properties( trackports );
	if( items == NULL ) {
		veejay_msg(VEEJAY_MSG_INFO ,"All ports freed.");
		vevo_port_free_(trackports);	
		return;
	}
	int i;
	for( i = 0; items[i] != NULL; i ++ ) {
		char *key = items[i];
		char *ref = vevo_property_get_string( trackports, key );
		if(ref ) veejay_msg(0, "never freed: %s, %s", key,ref );
		if( ref) free(ref);
		free(items[i]);
	}
	free(items);
	
	vevo_port_free_( trackports );
#else
	veejay_msg(0, "recompile with -DVEVO_TRACKPORTS");
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
		if( vevo_property_exists( port, pkey ) == 0 )
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
		if(vevo_property_get( port_a, Ea[i], 0, &v ) == VEVO_NO_ERROR ) {
			vevo_property_set( port_b, Ea[i],VEVO_ATOM_TYPE_VOIDPTR,1, &v );
		}
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


#ifdef STRICT_CHECKING
int		vevo_port_get_total_size_( vevo_port_t *port, int *n_ports )
{
	char **items = vevo_list_properties( port );
	if( items == NULL )
		return 0;
	int i;
	int msize = 0;
	for( i = 0; items[i] != NULL ; i ++ ) {
		msize += vevo_property_atom_size( port, items[i] );
		if( vevo_property_atom_type(port, items[i]) == VEVO_ATOM_TYPE_PORTPTR ) {
			void *child_port = NULL;
			if(vevo_property_get( port, items[i], 0, &child_port ) == VEVO_NO_ERROR ) {
				msize += vevo_port_get_total_size_( child_port, n_ports);
			}
		}
		free(items[i]);
	}
	free(items);

	__vevo_port_t *p = (__vevo_port_t*) port;
	if( p->pool ) {
		msize += vevo_pool_size(p->pool);
	}

	*n_ports ++;
	return msize;
}
int		vevo_port_get_total_size( vevo_port_t *port )
{
	int n_ports = 0;
	int total = vevo_port_get_total_size_( port, &n_ports );
	veejay_msg(VEEJAY_MSG_DEBUG, "\tPort %p holds %2.2f Kb memory",port,  ( (float) total / 1024.0f ));
	return total;
}


#else
int		vevo_port_get_total_size( vevo_port_t *port )
{
	return 0;
}
#endif

#ifdef STRICT_CHECKING
void	vevo_port_validate_ports( vevo_port_t *port , vevo_port_t *ref)
{
	if(port == NULL)
		return;       

	if( ref == NULL )
		ref = vpn ( VEVO_ANONYMOUS_PORT ); 

	int i;
	vevo_storage_t **item = vevo_list_nodes_( port, VEVO_ATOM_TYPE_PORTPTR );
	if(!item)
	{
		free(item);
		return;
	}

	for( i = 0; item[i] != NULL ; i ++ )
	{
		char key[64];
		void *q = NULL;
		int n = vevo_port_get_port( port, item[i], &q );	

		if( n == 1 && q != NULL )
		{	
			sprintf( key, "%p", q );
			if( (vevo_property_get( ref, key,0, NULL ) == VEVO_NO_ERROR ) ) {
				veejay_msg(0, " '%s' already exists in ref!", key );
				vevo_port_dump( ref, 0 );
				assert(0);
			}
			vevo_port_validate_ports( q, ref );	
		}
		else
		{
			if( n > 1 )
			{
				int k = 0;
				for( k = 0; k < item[i]->num_elements; k ++ )
				{
					void *qq = NULL;
					int err = atom_get_value( item[i], k, &qq );
					if( err != VEVO_NO_ERROR )
						continue;
					sprintf( key, "%p", qq );
					if(( vevo_property_get( ref, key,0,NULL ) == VEVO_NO_ERROR) ) {
						veejay_msg(0, " '%s' already exists in ref!", key );
						vevo_port_dump(ref,0);
						assert(0);
					}	
					vevo_port_validate_ports( qq, ref );

				}
			}
		}
	}
	
	char pkey[64];
	sprintf(pkey, "%p", port );

	vevo_property_set( ref, pkey, VEVO_ATOM_TYPE_STRING, 0, NULL );


	free(item);
}	



void	vevo_port_scan_free( vevo_port_t *port )
{
	if(port == NULL)
		return;       
	
   	__vevo_port_t *vp = (__vevo_port_t *) port;
	if( vp->index == NULL ) {	
		port = NULL;
		return;
	}

	int i;
	vevo_storage_t **item = vevo_list_nodes_( port, VEVO_ATOM_TYPE_PORTPTR );
	if(!item)
	{
		veejay_msg(0, " -> PORT %p", port );
		free(item);
		return;
	}

	for( i = 0; item[i] != NULL ; i ++ )
	{
		void *q = NULL;
		int n = vevo_port_get_port( port, item[i], &q );	

		if( n == 1 && q != NULL )
		{	
			veejay_msg(0, " -> PORT %p", q );
			vevo_port_scan_free( q );	
		}
		else
		{
			if( n > 1 )
			{
				int k = 0;
				for( k = 0; k < item[i]->num_elements; k ++ )
				{
					void *qq = NULL;
					int err = atom_get_value( item[i], k, &qq );
					if( err != VEVO_NO_ERROR )
						continue;
					veejay_msg(0, " -> PORT %p", qq );
					vevo_port_scan_free( qq );
				}
			}
		}
	}

	free(item);
}	


#endif

void	vevo_port_recursive_free( vevo_port_t *port )
{
	if(port == NULL)
		return;   

   	__vevo_port_t *vp = (__vevo_port_t *) port;
	if( vp->index == NULL ) {	
		port = NULL;
		return;
	}

	vevo_storage_t **item = vevo_list_nodes_( port, VEVO_ATOM_TYPE_PORTPTR );
	if(!item)
	{
		vevo_port_free_( port );
		free(item);
		return;
	}

	int i;

	for( i = 0; item[i] != NULL ; i ++ )
	{
		void *q = NULL;
		int n = vevo_port_get_port( port, item[i], &q );	

		if( n == 1 && q != NULL )
		{	
			vevo_port_recursive_free( q );	
		}
		else
		{
			if( n > 1 )
			{
				int k = 0;
				for( k = 0; k < item[i]->num_elements; k ++ )
				{
					void *qq = NULL;
					int err = atom_get_value( item[i], k, &qq );
					if( err != VEVO_NO_ERROR )
						continue;
					vevo_port_recursive_free( qq );
				}
			}
		}
	}

	free(item);
	vevo_port_free_( port );
	port = NULL;
}	
	
/*
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
		if( vevo_property_is_protected( port, top_level[k] ) ) {
			continue;
		}

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
*/
/*
static void vevo_port_do_recursion( vevo_port_t *port, vevo_port_t *namespace, const char *base )
{
	char **top_level = vevo_list_properties(port);
	if(!top_level)
		return;

	int k;
	int error;

	error = vevo_property_set( namespace, base, LIVIDO_ATOM_TYPE_VOIDPTR, 0 , NULL );

	for( k = 0; top_level[k] != NULL ; k ++ ) {
		int atom_type = vevo_property_atom_type( port, top_level[k] );
		//@ skip private properties
		if( atom_type == VEVO_ATOM_TYPE_VOIDPTR ) {
			free(top_level[k]);
			continue;
		}
		//@ skip protected properties
		if( vevo_property_is_protected( port, top_level[k] ) ) {
			free(top_level[k]);
			continue;
		}

		if( atom_type != VEVO_ATOM_TYPE_PORTPTR ) {
			//@ create path node

			char *path = vj_calloc(sizeof(char) * 256 );
			char *pathptr = vvstrcat( path, base );
			pathptr       = vvstrcat( pathptr, "/" );
			char *pathnode= vvstr_valid_osc_name( top_level[k] );
			pathptr       = vvstrcat( pathptr, pathnode );
			free(pathnode);
			error = vevo_property_set( namespace, path, LIVIDO_ATOM_TYPE_VOIDPTR, 0 , NULL );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif
			free(path);
		} else {
			void *child_port = NULL;
			error = vevo_property_get( port, top_level[k],0, &child_port );

			char *path = vj_calloc(sizeof(char) * 256 );
			char *pathptr = vvstrcat( path, base );
			pathptr       = vvstrcat( pathptr, "/");
			char *pathnode= vvstr_valid_osc_name( top_level[k] );
			pathptr	      = vvstrcat( pathptr,pathnode );
			free(pathnode);

			error = vevo_property_set( namespace, path, LIVIDO_ATOM_TYPE_VOIDPTR, 0 , NULL );
#ifdef STRICT_CHECKING
			assert( error == VEVO_NO_ERROR );
#endif

			vevo_port_do_recursion( child_port, namespace, path );

			free(path);
			
		}
		
		free(top_level[k]);
	}
	free(top_level);
}


void *vevo_port_recurse_namespace( vevo_port_t *port, const char *base )
{
	void *ns = vpn( VEVO_ANONYMOUS_PORT );

	vevo_port_do_recursion( port, ns, base );

	return ns;
}

*/
//@ FIXME: method to recursivly iterate a port and all subports to size-up all data, vevo_port_get_total_size()

//! Flatten all ports and return list of ports to be destroyed
/*!
 \param sorted_port Reference Port
 \param p Top level Port to scan
 */
static void	vevo_port_recurse_free( vevo_port_t *sorted_port, vevo_port_t *p )
{
	vevo_storage_t **item = vevo_list_nodes_( p, VEVO_ATOM_TYPE_PORTPTR );
	if(!item)
	{
		return;
	}

//	vevo_port_register( sorted_port, p );

	int i;
	for( i = 0; item[i] != NULL ; i ++ )
	{
		void *q = NULL;
		int n = vevo_port_get_port( p, item[i], &q );	

		if( n == 1 && q != NULL )
		{	
			if(!vevo_scan_for_atom( q, VEVO_ATOM_TYPE_PORTPTR ))
			{
				vevo_port_register( sorted_port,q );
				vevo_port_recurse_free( sorted_port,q );
			}
		}
		else
		{
			if( n > 1 )
			{
				int k = 0;
				for( k = 0; k < item[i]->num_elements; k ++ )
				{
					void *qq = NULL;
					int err = atom_get_value( item[i], k, &qq );
					if( err != VEVO_NO_ERROR )
						continue;

					if(!vevo_scan_for_atom(qq,VEVO_ATOM_TYPE_PORTPTR )) {
						vevo_port_register( sorted_port,qq );
						vevo_port_recurse_free( sorted_port,qq );
					}
				}
			}
		}

	}
	free(item);
}

static	char	*vevo_property_get_str( vevo_port_t *port, const char *key )
{
  	size_t len = vevo_property_element_size( port, key,0 );
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
		case VEVO_ATOM_TYPE_UTF8STRING:
			token[0] = 'S';
			break;

	}
		
	if( token[0])
	{
		int len = n_elem * strlen(token) + 1;
		res = (char*) calloc(1, sizeof(char) * len );
		/*for( i =0; i < n_elem; i ++ ) {
			while( *res ) *res ++;
			while( *res++ = *token++ );
			res--;
		} FIXME: test me
		*/
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
		case VEVO_ATOM_TYPE_UTF8STRING:
			token[0] = 'S';
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
		case VEVO_ATOM_TYPE_UTF8STRING:
			token[0] = 'S';
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

	if( atom == VEVO_ATOM_TYPE_FUNCPTR ) {
		void *drv = NULL;
		int err = vevo_property_get( port, "driver", 0, &drv );
		if( drv == NULL || err != VEVO_NO_ERROR ) {
			veejay_msg(0, "No context for property '%s'", s );
			return 0;
		}

		int32_t v;
		if( sscanf( value, "%d", &v ) ) {
			vevo_property_call( port, key, drv, v,v );
			return 1;
		}
		return 0;
	}

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
					n = sscanf( arg, "%" SCNd64, &(i64_val[cur_elem]));
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
	char val[64];
	int n = vevo_property_num_elements( port, key );
	if( n <= 0 ) {
		return strdup("<empty>");
	}
	
	int i;
	int atom = vevo_property_atom_type( port , key );

	memset(tmp,0,sizeof(tmp));
	memset(val,0,sizeof(val));

	if(atom == VEVO_ATOM_TYPE_INT || atom == VEVO_ATOM_TYPE_BOOL)
	{
		int *a = (int*) vj_calloc(sizeof(int) * n );
		for( i = 0; i < n ; i ++ ) {
			if( vevo_property_get( port, key, i, &(a[i]) ) != VEVO_NO_ERROR)
			{
				free(a);
				return NULL;
			}
			else
			{
				sprintf(val, "'%d'", a[i]);
				strcat( tmp, val );
				strcat( tmp, " ");
			}
		}
		free(a);
	} else if (atom == VEVO_ATOM_TYPE_DOUBLE )
	{
		double *a = (double*) vj_calloc(sizeof(double) * n );
		for( i = 0; i < n; i ++ ) {
			if( vevo_property_get( port, key, i , &(a[i])) != VEVO_NO_ERROR)
			{
				free(a);
				return NULL;
			}
			else
			{
				sprintf(val, "'%g'", a[i]);
				strcat( tmp, val );
				strcat( tmp, " " );
			}
		}
		free(a);
	} else if ( atom == VEVO_ATOM_TYPE_UINT64 ) {
	       uint64_t *a = (uint64_t*) vj_calloc(sizeof(uint64_t)*n);
	       for( i = 0; i < n; i ++ )  {
	         if( vevo_property_get(port,key,i, &(a[i])) != VEVO_NO_ERROR ) {
		  free(a);
		  return NULL;
		   }
			 else {
				      sprintf(val, "'%" PRId64 "'", a[i]);
				      strcat(tmp,val);
				      strcat(tmp," ");
	 		}
		   }
		   free(a);
	}
	else if ( atom == VEVO_ATOM_TYPE_STRING || atom == VEVO_ATOM_TYPE_UTF8STRING ) {
		   size_t len = vevo_property_element_size( port, key ,0);
	   		if( len > 0 ) {
				char *strv = vevo_property_get_string(port,key);
				strcat(tmp, strv );
				strcat(tmp, " ");
				free(strv);
			}
	} else 
	{
		return NULL;
	}
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
					sprintf(tmp, "%" PRId64 ":", i64_val );
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
			case 'S':
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
					n = sscanf( arg, "%" PRId64, &(i64_val[cur_elem]));
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
        size_t len = vevo_property_element_size( port, key,0 );
        char *ret = NULL;

	if(len<=0) 
		return NULL;

	if( vevo_property_get( port, key,0,NULL ) != VEVO_NO_ERROR )
		return NULL;

#ifdef STRICT_CHECKING	
	int at = vevo_property_atom_type( port, key );
	assert( at == VEVO_ATOM_TYPE_STRING || at == VEVO_ATOM_TYPE_UTF8STRING );
#endif

	ret = (char*) vj_malloc(sizeof(char) * len );
	if(ret == NULL) {
		return NULL;
	}

	int err = vevo_property_get( port, key, 0, &ret );
	if( err != VEVO_NO_ERROR ) {
		if(ret) free(ret);
		return NULL;
	}
	//@ string stored in utf8
//n	if( at == VEVO_ATOM_TYPE_UTF8STRING ) {
//		gsize in,out;
//		gchar *lcstr = g_locale_from_utf8( ret, len, &in, &out, NULL );
//		free(ret);
//		return lcstr;
//	}

        return ret;
}

char            *vevo_property_get_utf8string( void *port, const char *key )
{
/*        size_t len = vevo_property_element_size( port, key ,0);
        char *ret = NULL;
        if(len<=0) return NULL;

	if( vevo_property_get( port, key,0,NULL ) != VEVO_NO_ERROR )
		return NULL;

	int at = vevo_property_atom_type(port,key);

#ifdef STRICT_CHECKING
	assert( at == VEVO_ATOM_TYPE_STRING|| at == VEVO_ATOM_TYPE_UTF8STRING );
#endif
        ret = (char*) vj_malloc(sizeof(char) * len );
        vevo_property_get( port, key, 0, &ret );

	if( at == VEVO_ATOM_TYPE_STRING )
	{
		gsize in = 0;
		gsize out = 0;
		gchar *utf8_str = g_locale_to_utf8( (const gchar*) ret, len, &in, &out, NULL );
		free(ret);
	        return (char*) utf8_str;

	}
	return ret;	*/
	return NULL;
}

char	**vevo_property_get_string_arr( vevo_port_t *p, const char *key )
{
	int n = vevo_property_num_elements( p, key );
	if( n == 0 )
		return NULL;
	char **retval = vj_malloc(sizeof(char*) * n );
	if( retval == NULL )
		return NULL;
	int i;
	for( i = 0; i < n ; i ++ ) {
		retval[i] = vj_calloc( vevo_property_element_size(p,key,i) + 1 );
		int err = vevo_property_get( p, key, i, &retval[i] );
		if( err != VEVO_NO_ERROR ) {
			for( -- i; i >= 0; i -- ) {
				free( retval[i] );
			}
			free(retval);
			return NULL;
		}
	}
	return retval;
}

int
vevo_property_del(vevo_port_t * p,
		    const char *key )
{
#ifdef VEVO_TRACKPORTS
	veejay_msg(VEEJAY_MSG_DEBUG,"%s: key=%s", __FUNCTION__, key );
#endif
#ifdef STRICT_CHECKING
    assert(p != NULL);
	//@ no self referencing
    assert( key != NULL );
	//@ deleting ports not allowed in strict mode
	assert( vevo_property_atom_type(p, key ) != VEVO_ATOM_TYPE_PORTPTR );
#endif
    __vevo_port_t *port = (__vevo_port_t *) p;

	//@ find value in hash and release from pool

    ukey_t hash_key = hash_key_code(key);
    if (!port->table) {
		vevo_property_t *pnode = NULL;
		if ((pnode = prop_node_get(port, hash_key)) != NULL) {
		    vevo_free_storage(port,pnode->st);
		    prop_node_free(port,pnode);
		}
  	} else {
		hnode_t *old_node = NULL;
		if ((old_node = property_exists(port, hash_key)) != NULL) {
	   		 vevo_storage_t *oldstor = (vevo_storage_t *) hnode_get(old_node);
	   	 	 vevo_free_storage(port,oldstor);
	   		 hash_delete((hash_t *) port->table, old_node);
	   	     hnode_destroy(old_node);
		}
    }

	if( port->index == NULL )
			return VEVO_NO_ERROR;

	//@ find cached hash key in index

    port_index_t *list = port->index;
    port_index_t *node = list;
    port_index_t *prev = NULL;
    
	do 
    {
		ukey_t idxkey = hash_key_code( node->key );
		if( idxkey == hash_key ) {
			//node at beginning of list?
			if( node == list ) {
				port->index = NULL;
				if( node->next != NULL )
						port->index = node->next;
				
				port_node_free(port, node);
				return VEVO_NO_ERROR;
			}
			else if ( node != list && node->next != NULL && prev != NULL ) { 
				prev->next = node->next; //@ fix pointer of previous next to current next
				port_node_free(port, node );
				return VEVO_NO_ERROR;
			}
			else if ( node->next == NULL && node != list && prev != NULL ) {
				//at the end
				prev->next = NULL; //@ fix pointer of previous to new end of list
				port_node_free( port, node );
				return VEVO_NO_ERROR;
			}
		}
		prev = node;
		node = node->next;
    } while( node != NULL );

    return VEVO_ERROR_NOSUCH_PROPERTY;
}

char	*vevo_tabs( int lvl ) {
	char tmp[32];
	int i;
	for( i = 0; i < lvl; i ++ )
			tmp[i] = '\t';
	tmp[lvl] = '\0';
	return strdup(tmp);
}

static struct  { //@ FIXME move to specific locations, use a register to register new port types and names
	int id;
	char *name;

} port_types[] = 
{
	{ 1, "Livido Plugin Info" },
	{ 2, "Livido Filter Class" },
	{ 3, "Livido Instance" },
	{ 4, "Livido Channel Template" },
	{ 5, "Livido Parameter Template" },
	{ 6, "Livido Channel" },
	{ 7, "Livido Parameter" },
	{ 8, "Livido GUI" },
	{10, "FreeFrame Port" },
	{11, "FreeFrame Parameter" },
	{20, "Frei0r Port"},
	{21, "Frei0r Parameter"},
	{30, "VEVO Livido Port"},
	{31, "VEVO Livido Parameter Port"},
	{100,"VEVO Illegal List"},
	{321,"VEVO Event Port"},
	{32, "VEVO Veejay FX"},
	{40, "VEVO Cache"},
	{1040, "VEVO Port Reference"},
	{2035, "VEVO Sample Port"},
	{2036, "VEVO Sample Bank Port"},
	{33,   "VEVO Veejay FX Instance"},
	{-1,   "VEVO Anonymous Port"},
	{0,	   NULL }
};

static	char *	vevo_identify_port( int type ) {
	int i;
	for( i = 0; port_types[i].name != NULL ; i ++ ) {
			if( type == port_types[i].id )
					return port_types[i].name;
	}
	return "Illegal or unknown port type";
}

void	vevo_port_dump( void *p, int lvl )
{
	char **keys = vevo_list_properties(p);
	int k;

	if( keys == NULL ) {
		veejay_msg(0, "Port %p is empty",p);
		return;
	}

	void *voidval = NULL;
	char *value   = NULL;
	int err = 0;

	char *tabs = NULL;
    if( lvl > 0 )
		tabs = vevo_tabs( lvl );

#ifdef STRICT_CHECKING
	veejay_msg(VEEJAY_MSG_DEBUG, "%s%p (%d bytes)", (tabs == NULL ? "/" : tabs ), p,
			vevo_port_get_total_size(p));
#else
	veejay_msg( VEEJAY_MSG_DEBUG, "%s%p", (tabs == NULL ? "/" : tabs ), p );
#endif

	for( k = 0; keys[k] != NULL; k ++ ) {

	    if( strncasecmp( keys[k], "type",4 ) == 0 ) {
			int tval = 0;
			err = vevo_property_get( p, keys[k],0,&tval );
			veejay_msg(VEEJAY_MSG_DEBUG, "%s %s %s",
							(tabs==NULL ? "->" : tabs ), keys[k], vevo_identify_port(tval));
			free( keys[k] );
			continue;
		}

		int at = vevo_property_atom_type(p,keys[k]);
		switch(at) {
			case VEVO_ATOM_TYPE_PORTPTR:
				err = vevo_property_get(p, keys[k], 0, &voidval );
				if( err == VEVO_NO_ERROR ) {
						veejay_msg(VEEJAY_MSG_DEBUG, "%s %s:",
										(tabs == NULL ? "->" : tabs ), keys[k]);
						vevo_port_dump(voidval, lvl + 1);
				} else {
						veejay_msg(VEEJAY_MSG_DEBUG, "%s error code %d",
										keys[k], err );
				}
				break;
			case VEVO_ATOM_TYPE_VOIDPTR:
				err = vevo_property_get( p, keys[k], 0, &voidval );
				if( err == VEVO_NO_ERROR ) {
						veejay_msg(VEEJAY_MSG_DEBUG, "%s %s VOID*",
										(tabs==NULL ? "->" : tabs ), keys[k]);
				} else {
						veejay_msg(VEEJAY_MSG_DEBUG, "%s %s error code %d",
										(tabs==NULL ? "->" : tabs ), keys[k],err);
				}
				break;
			default:
				value = vevo_sprintf_property_value( p, keys[k] );
				if( value == NULL ) {
						veejay_msg(VEEJAY_MSG_DEBUG, "%s %s no value",
										(tabs == NULL ? "->" : tabs), keys[k] );
				} else {
						veejay_msg(VEEJAY_MSG_DEBUG, "%s %s (%d elements) %s",
										(tabs == NULL ? "->" : tabs ), keys[k], 
										vevo_property_num_elements( p,keys[k]),
										value );
						free(value);
				}
				break;
		}
		free(keys[k]);
	}
	if(tabs) free(tabs);
	free(keys);

}

int		vevo_property_clone( void *port, void *to_port, const char *key, const char *as_key )
{
	const int n = vevo_property_num_elements(port, key);
	const int t = vevo_property_atom_type( port, key );
	int i;
	if( n <= 0 ) {
		if( n == 0 ) 
			return VEVO_ERROR_PROPERTY_EMPTY;
		return VEVO_ERROR_NOSUCH_ELEMENT;
	}

	switch(t) {
		case VEVO_ATOM_TYPE_INT:
		case VEVO_ATOM_TYPE_BOOL:
			{
				int32_t *tmp = (int32_t*) malloc(sizeof(int32_t) * n );
				for(i = 0; i < n; i ++ ) {
					if( vevo_property_get( port, key, i, &(tmp[i])) != VEVO_NO_ERROR ) {
#ifdef STRICT_CHECKING
						veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s -> %s element %d does not exist but reported size is %d elements",
								__FUNCTION__,
								key,
								as_key,
								i,
								n );
#endif
						free(tmp);
						return VEVO_ERROR_NOSUCH_ELEMENT;
					}
				}
				if( n > 0 ) {
					if( vevo_property_set( to_port, as_key, t, n, tmp ) != VEVO_NO_ERROR ) {
#ifdef STRICT_CHECKING
						veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s -> %s unable to store %d elements (%d bytes)", __FUNCTION__, key,as_key, n, (n*sizeof(int32_t)));
#endif
						free(tmp);
						return VEVO_ERROR_MEMORY_ALLOCATION;
					}
				} else if (n == 0 ) {
					vevo_property_set( to_port, as_key, t, 0, NULL );
				}
				free(tmp);
			}
			break;
		case VEVO_ATOM_TYPE_DOUBLE:
			{
				double *tmp = (double*) malloc(sizeof(double) * n );
				for(i = 0; i < n; i ++ ) {
					if( vevo_property_get( port, key, i, &(tmp[i])) != VEVO_NO_ERROR ) {
#ifdef STRICT_CHECKING
						veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s -> %s element %d does not exist but reported size is %d elements",
								__FUNCTION__,
								key,
								as_key,
								i,
								n );
#endif
						free(tmp);
						return VEVO_ERROR_NOSUCH_ELEMENT;
					}
				}

				if( n > 0 ) {
					if( vevo_property_set( to_port, as_key, t, n, tmp ) != VEVO_NO_ERROR ) {
#ifdef STRICT_CHECKING
					veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s -> %s unable to store %d elements (%d bytes)", __FUNCTION__, key,as_key, n, (n*sizeof(double)));
#endif
					free(tmp);
					return VEVO_ERROR_MEMORY_ALLOCATION;
					}
				} else if ( n == 0 ) {
					vevo_property_set(to_port, as_key, t, 0, NULL );
				}
				free(tmp);
			}
			break;
		case VEVO_ATOM_TYPE_UINT64:
			{
				uint64_t *tmp = (uint64_t*) malloc(sizeof(uint64_t) * n );
				for(i = 0; i < n; i ++ ) {
					if( vevo_property_get( port, key, i, &(tmp[i])) != VEVO_NO_ERROR ) {
#ifdef STRICT_CHECKING
						veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s -> %s element %d does not exist but reported size is %d elements",
								__FUNCTION__,
								key,
								as_key,
								i,
								n );
#endif
						free(tmp);
						return VEVO_ERROR_NOSUCH_ELEMENT;
					}
				}

				if( n > 0 ) {
					if( vevo_property_set( to_port, as_key, t, n, tmp ) != VEVO_NO_ERROR ) {
#ifdef STRICT_CHECKING
					veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s -> %s unable to store %d elements (%d bytes)", __FUNCTION__, key,as_key, n, (n*sizeof(double)));
#endif
					free(tmp);
					return VEVO_ERROR_MEMORY_ALLOCATION;
					}
				} else if ( n == 0 ) {
					vevo_property_set(to_port, as_key, t, 0, NULL );
				}
				free(tmp);
			}
			break;

		case VEVO_ATOM_TYPE_STRING:
		case VEVO_ATOM_TYPE_UTF8STRING:
			{
				if( n == 1 ) {
					char *src = vevo_property_get_string( port, key );
					if( vevo_property_set(to_port, as_key, t, 1,&src ) != VEVO_NO_ERROR ) {
#ifdef STRICT_CHECKING
						veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s -> %s unable to store string '%s'",
								__FUNCTION__, key,as_key,src );
#endif
						free(src);
						return VEVO_ERROR_MEMORY_ALLOCATION;
					}
				}
#ifdef STRICT_CHECKING
				else if ( n > 1 ) {
					assert( n <= 0 ); // array of strings support 
				}
#endif
				else if ( n == 0 ) {
					if( vevo_property_set(to_port,as_key,t,0,NULL ) != VEVO_NO_ERROR ) {
						return VEVO_ERROR_PROPERTY_EMPTY;
					}
				}
			}
			break;
		default:
#ifdef STRICT_CHECKING
				veejay_msg(VEEJAY_MSG_DEBUG, "%s: %s cannot be cloned.",__FUNCTION__,key );
#endif
				break;

	}
	

	return VEVO_NO_ERROR;
}

/*
void	vevo_dom_dump( void *p, FILE *file )
{
	char **keys = vevo_list_properties(p);
	int k;

	if( keys == NULL ) {
		veejay_msg(0, "Port %p is empty",p);
		return;
	}

	void *voidval = NULL;
	char *value   = NULL;
	int err = 0;

	char *tabs = NULL;

	if( lvl == 0 || file == NULL ) {
		file = fopen( "rw" ,"/tmp/port1.xml" );
	}

	for( k = 0; keys[k] != NULL; k ++ ) {
	    if( strncasecmp( keys[k], "type",4 ) == 0 ) {
		int tval = 0;
		err = vevo_property_get( p, keys[k],0,&tval );
		fprintf( file,"<p>%s</p>", vevo_identify_port(tval));
		continue;
	    }

	    int at = vevo_property_atom_type(p,keys[k]);
	    switch(at) {
		case VEVO_ATOM_TYPE_PORTPTR:
			err = vevo_property_get(p, keys[k], 0, &voidval );
			if( err == VEVO_NO_ERROR ) {
				fprintf(file,"<ul><li>%s</li>", keys[k] );
				vevo_dom_dump(voidval, file);
				fprintf(file,"</ul>");
			} else {
				fprintf(file,"<ul><li>%s with error %s</li>", keys[k] );
			}
			break;
			case VEVO_ATOM_TYPE_VOIDPTR:
				err = vevo_property_get( p, keys[k], 0, &voidval );
				if( err == VEVO_NO_ERROR ) {
					fprintf( file, "<li><b>private</b></li>");

				} else {
					fprintf( file, "<li><b>private with error %s</b></li>",keys[k]);
				}
				break;
			default:
				value = vevo_sprintf_property_value( p, keys[k] );
				fpritnf( file, "<li><table><tr><td>%s</td><td>%s</td></tr></table></li>",keys[k],value);
				break;
		}
		free(keys[k]);
	}
	if(tabs) free(tabs);
	free(keys);

	if( lvl == 0 )
		fclose(file);	
}*/


int	vevo_write_xml( vevo_port_t *root, const char *destination, char *hiearchy[] )
{
	char **properties = vevo_list_properties( root );
	if( properties == NULL )
		return 0;
/*	xmlNodePtr childnode;
	xmlDocPtr doc = xmlNewDoc( "1.0" );
	xmlNodePtr rootnode =
		xmlNewDocNode( doc, NULL , (const xmlChar*) hiearchy[0], NULL );
	xmlDocSetRootElement( doc, rootnode );

	int i = 0;
	int level = 1;
	while( properties[i] != NULL )
	{
		int atom_type = vevo_property_atom_type( root, properties[i] );
		void *port = NULL;
		
				childnode = xmlNewChild( rootnode, NULL, (const xmlChar*)
						hierachy[ level ], NULL );
				vevo_property_get( port, properties[i], 0, &port );
		

		i++;
	}

	xmlNodePtr childnode = xmlNewChild( rootnode, NULL, (const xmlChar*) "timeline", NULL );
*/

	return 1;	

}

