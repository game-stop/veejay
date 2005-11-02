#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <include/vevo.h>
#include <include/livido.h>

/**
	VeVo's mediation layer and core livido functions implementation
	using hashtable, see hash.h	
	
 */

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

/* forward declarations */

static int property_exists(livido_port_t * port, const char *key);

/**
	local functions, used to copy an atom's value to some destination
 */

static inline void parse_pv(atom_t * t, void *dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->value != NULL);
    assert(dst != NULL);
#endif
    memcpy(dst, t->value, sizeof(void *));
}

static inline void parse_d(atom_t * t, double *dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->value != NULL);
    assert(dst != NULL);
#endif
    *dst = *((double *) t->value);
}

static inline void parse_i(atom_t * t, int32_t * dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->value != NULL);
    assert(dst != NULL);
#endif
    *dst = *((int *) t->value);
}

static inline void parse_b(atom_t * t, int32_t * dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->value != NULL);
    assert(dst != NULL);
#endif
    *dst = *((int32_t *) t->value);
}

static inline void parse_s(atom_t * t, char *dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->value != NULL);
    assert(dst != NULL);
    assert(t->size - 1 > 0);
#endif
    memcpy(dst, t->value, t->size - 1);
}

static inline void parse_pp(atom_t * t, livido_port_t * dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->value != NULL);
    assert(dst != NULL);
#endif
    memcpy(dst, t->value, sizeof(livido_port_t *));
}

static int __atom_get_value(int atom_type, atom_t * t, void *dst)
{
    if (t->size == 0 || dst == NULL)
	return LIVIDO_NO_ERROR;

    switch (atom_type) {
    case LIVIDO_ATOM_TYPE_INT:
	parse_i(t, dst);
	break;
    case LIVIDO_ATOM_TYPE_STRING:
	parse_s(t, dst);
	break;
    case LIVIDO_ATOM_TYPE_DOUBLE:
	parse_d(t, dst);
	break;
    case LIVIDO_ATOM_TYPE_BOOLEAN:
	parse_b(t, dst);
	break;
    case LIVIDO_ATOM_TYPE_VOIDPTR:
	parse_pv(t, dst);
	break;
    case LIVIDO_ATOM_TYPE_PORTPTR:
	parse_pp(t, dst);
	break;
    default:
	return LIVIDO_ERROR_WRONG_ATOM_TYPE;
    }

    return LIVIDO_NO_ERROR;
}

/**
	local functions used to access an array of atoms in livido_storage_t
 */
static inline int get_int(void *dst, livido_storage_t * t, int idx)
{
    int32_t *arr = dst;
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->elements.array[idx]);
#endif
    __atom_get_value(t->atom_type, t->elements.array[idx], &arr[idx]);

    return LIVIDO_NO_ERROR;
}

static inline int get_double(void *dst, livido_storage_t * t, int idx)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->elements.array[idx]);
#endif
    double *arr = dst;
    __atom_get_value(t->atom_type, t->elements.array[idx], &arr[idx]);
    return LIVIDO_NO_ERROR;
}

static inline int get_string(void *dst, livido_storage_t * t, int idx)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->elements.array[idx]);
#endif
    char **arr = (char **) dst;
    __atom_get_value(t->atom_type, t->elements.array[idx], arr[idx]);

    return LIVIDO_NO_ERROR;
}

static inline int get_boolean(void *dst, livido_storage_t * t, int idx)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->elements.array[idx]);
#endif
    int32_t *arr = dst;
    __atom_get_value(t->atom_type, t->elements.array[idx], &arr[idx]);

    return LIVIDO_NO_ERROR;
}

static inline int get_voidptr(void *dst, livido_storage_t * t, int idx)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->elements.array[idx]);
#endif
    void **arr = (void **) dst;
    __atom_get_value(t->atom_type, t->elements.array[idx], &arr[idx]);
    return LIVIDO_NO_ERROR;
}

static inline int get_portptr(void *dst, livido_storage_t * t, int idx)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->elements.array[idx]);
#endif
    livido_port_t **arr = (livido_port_t **) dst;
    __atom_get_value(t->atom_type, t->elements.array[idx], &arr[idx]);
    return LIVIDO_NO_ERROR;
}

static int __get_all_atoms(int atom_type, livido_storage_t * t, void *dst)
{
    int i;
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->num_elements > 1);
#endif
    switch (t->atom_type) {
    case LIVIDO_ATOM_TYPE_INT:
	for (i = 0; i < t->num_elements; i++)
	    get_int(dst, t, i);
	return LIVIDO_NO_ERROR;
    case LIVIDO_ATOM_TYPE_DOUBLE:
	for (i = 0; i < t->num_elements; i++)
	    get_double(dst, t, i);
	return LIVIDO_NO_ERROR;
    case LIVIDO_ATOM_TYPE_STRING:
	for (i = 0; i < t->num_elements; i++)
	    get_string(dst, t, i);
	return LIVIDO_NO_ERROR;
    case LIVIDO_ATOM_TYPE_BOOLEAN:
	for (i = 0; i < t->num_elements; i++)
	    get_boolean(dst, t, i);
	return LIVIDO_NO_ERROR;
    case LIVIDO_ATOM_TYPE_VOIDPTR:
	for (i = 0; i < t->num_elements; i++)
	    get_voidptr(dst, t, i);
	return LIVIDO_NO_ERROR;
    case LIVIDO_ATOM_TYPE_PORTPTR:
	for (i = 0; i < t->num_elements; i++)
	    get_portptr(dst, t, i);
	return LIVIDO_NO_ERROR;
    default:
	break;
    }

    return LIVIDO_ERROR_WRONG_ATOM_TYPE;
}
static int __get_by_index(livido_storage_t * t, void *dst, int idx)
{
    if (idx < 0 || idx >= t->num_elements)
	return LIVIDO_ERROR_NOSUCH_ELEMENT;
#ifdef STRICT_CHECKING
    assert(t != NULL);
    assert(t->elements.array[idx]);
#endif

    __atom_get_value(t->atom_type, t->elements.array[idx], dst);

    return LIVIDO_NO_ERROR;
}

/**
	local functions used to finalize a property (sets the READONLY flag)
	Pending: rename READONLY to FINAL 
 */
static int livido_property_finalize(livido_port_t * port, const char *key)
{
    if (property_exists(port, key)) {
	hnode_t *node = hash_lookup(port->table, (const void *) key);
	if (node) {
	    livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
	    stor->flags |= LIVIDO_PROPERTY_READONLY;
	    hnode_t *new_node = hnode_create((void *) stor);
	    hnode_put(new_node, (void *) key);
	    hnode_destroy(new_node);
	}
    }

    return LIVIDO_NO_ERROR;
}

/**
	local function to get a value from an atom
 */

static int atom_get_value(livido_storage_t * t, int idx, void *dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
#endif
    if (idx < -1 || idx >= t->num_elements)
	return LIVIDO_ERROR_NOSUCH_ELEMENT;
    if (t->num_elements == 1 && idx == 0) {
#ifdef STRICT_CHECKING
	assert(t->elements.atom != NULL);
#endif
	__atom_get_value(t->atom_type, t->elements.atom, dst);
	return LIVIDO_NO_ERROR;
    }

    if (t->num_elements > 1 && idx == -1) {
#ifdef STRICT_CHECKING
	assert(t->elements.array != NULL);
#endif
	return __get_all_atoms(t->atom_type, t, dst);
    }

    if (t->num_elements > 1 && idx < t->num_elements)
	return __get_by_index(t, dst, idx);
    return LIVIDO_ERROR_NOSUCH_ELEMENT;
}

/**
	local function to get the size of a fundemental type
 */
static size_t livido_atom_size(int atom_type)
{
    switch (atom_type) {
    case LIVIDO_ATOM_TYPE_DOUBLE:
	return sizeof(double);
    case LIVIDO_ATOM_TYPE_INT:
	return sizeof(int32_t);
    case LIVIDO_ATOM_TYPE_VOIDPTR:
	return sizeof(void *);
    case LIVIDO_ATOM_TYPE_STRING:
	return 0;
    case LIVIDO_ATOM_TYPE_PORTPTR:
	return sizeof(livido_port_t *);
    case LIVIDO_ATOM_TYPE_BOOLEAN:
	return sizeof(int32_t);
    default:
#ifdef STRICT_CHECKING
	assert(0);
#endif
	break;
    }

    return 0;
}

/**
	local function to create a new atom.
	Atoms can be empty
 */

static atom_t *livido_new_atom(int atom_type, size_t atom_size)
{
    atom_t *atom = (atom_t *) malloc(sizeof(atom_t));
#ifdef STRICT_CHECKING
    assert(atom != NULL);
#endif
    atom->size = atom_size;
    atom->value = NULL;
    if (atom_size > 0) {
	atom->value = (void *) malloc(atom_size);
#ifdef STRICT_CHECING
	assert(atom->value != NULL);
#endif
	memset(atom->value, 0, atom_size);
    }
    return atom;
}

/**
	local function to free the atom
 */
static void livido_free_atom(atom_t * atom)
{
#ifdef STRICT_CHECKING
    assert(atom != NULL);
#endif
    if (atom) {
	free(atom->value);
	free(atom);
    }
    atom = NULL;
}

/**
	local function to create a new atom from a single value 
 */
static atom_t *livido_put_atom(void *dst, int atom_type)
{
    atom_t *atom = NULL;
    size_t atom_size = livido_atom_size(atom_type);
    if (atom_type == LIVIDO_ATOM_TYPE_STRING) {
	char *s = *((char **) dst);
	atom_size = strlen(s) + 1;
	atom = livido_new_atom(atom_type, atom_size);
	if (atom_size > 0)
	    memcpy(atom->value, s, (atom_size - 1));
    } else {
#ifdef STRICT_CHECKING
	assert(atom_size > 0);
	assert(dst != NULL);
#endif
	atom = livido_new_atom(atom_type, atom_size);
#ifdef STRICT_CHECING
	assert( atom != NULL );
#endif
	memcpy(atom->value, dst, atom_size);
    }
    return atom;
}

/**
	local function to create a new storage from a single or array of value
*/
static void
storage_put_atom_value(void *src, int n, livido_storage_t * d, int v)
{
    int i;
#ifdef STRICT_CHECKING
    if (n > 0)
	assert((src != NULL));
#endif
    if (d->num_elements >= 0) {
	if (d->num_elements >= 0 && d->num_elements <= 1) {
	    if (d->elements.atom)
		livido_free_atom(d->elements.atom);
	} else if (d->num_elements > 1) {
	    if (d->elements.array) {
		for (i = 0; i < d->num_elements; i++)
		    livido_free_atom(d->elements.array[i]);
		free(d->elements.array);
	    }
	}
    }

    d->atom_type = v;
    d->num_elements = n;
    // empty atom
    if (n == 0) {
	d->elements.atom = livido_new_atom(v, livido_atom_size(v));
    }
    // single atom
    if (n == 1) {
	d->elements.atom = livido_put_atom(src, v);
    }
    // array of atoms
    if (n > 1) {
	d->elements.array = (atom_t **) malloc(sizeof(atom_t *) * n);
	if (v == LIVIDO_ATOM_TYPE_DOUBLE) {
	    double *arr = src;
	    for (i = 0; i < d->num_elements; i++)
		d->elements.array[i] = livido_put_atom(&arr[i], v);
	}
	if (v == LIVIDO_ATOM_TYPE_STRING) {
	    char **arr = src;
	    for (i = 0; i < d->num_elements; i++)
		d->elements.array[i] = livido_put_atom(&arr[i], v);
	}
	if (v == LIVIDO_ATOM_TYPE_INT) {
	    int32_t *arr = src;
	    for (i = 0; i < d->num_elements; i++)
		d->elements.array[i] = livido_put_atom(&arr[i], v);
	}
	if (v == LIVIDO_ATOM_TYPE_BOOLEAN) {
	    int32_t *arr = src;
	    for (i = 0; i < d->num_elements; i++)
		d->elements.array[i] = livido_put_atom(&arr[i], v);
	}
	if (v == LIVIDO_ATOM_TYPE_VOIDPTR) {
	    void **arr = (void **) src;
	    for (i = 0; i < d->num_elements; i++)
		d->elements.array[i] = livido_put_atom(&arr[i], v);
	}
	if (v == LIVIDO_ATOM_TYPE_PORTPTR) {
	    livido_port_t **arr = (livido_port_t **) src;
	    for (i = 0; i < d->num_elements; i++)
		d->elements.array[i] = livido_put_atom(&arr[i], v);
	}
    }
}

/**
	local function to create a new storage that can hold N elements
 */
static livido_storage_t *livido_new_storage(int num_elements)
{
    livido_storage_t *d =
	(livido_storage_t *) malloc(sizeof(livido_storage_t));
    memset(d, 0, sizeof(livido_storage_t));
    d->num_elements = num_elements;
    return d;
}

/**
	local function to free a storage, deletes all atoms inside 
*/
static void livido_free_storage(livido_storage_t * t)
{
    if (t) {
	if (t->num_elements > 1) {
	    int i;
	    for (i = 0; i < t->num_elements; i++)
		livido_free_atom(t->elements.array[i]);
	    free(t->elements.array);
	}
	if (t->num_elements <= 1)
	    livido_free_atom(t->elements.atom);
	free(t);
    }
    t = NULL;
}
static unsigned int
hash_string(const void *data, unsigned int size)
{
    const char *s = data;
    unsigned int n = 0;
    unsigned int j = 0;
    unsigned int i = 0;
    while (*s) {
	j++;
	n ^= 271 * (unsigned) *s++;
    }
    i = n ^ (j * 271);
    return i % size;
}

/**
	local function to compare keys in hash
*/
static inline int key_compare(const void *key1, const void *key2)
{
    return strcmp(key1, key2);
}

/**
	local function to see if a property exists in hash
*/
static int property_exists(livido_port_t * port, const char *key)
{
    if (key == NULL)
	return 0;

    hnode_t *node = hash_lookup(port->table, (const void *) key);
    if (node) {
	if (hnode_get(node))
	    return 1;
    }
    return 0;
}

/**
	Livido core functions from version 100

The return values of livido_property_num_elements(), livido_property_element_size(), livido_property_atom_type() are all undefined if the property does not exist.

	int livido_property_num_elements (livido_port_t *port, const char *key)


pending change: take out livido_get_readonly
 */

int livido_property_num_elements(livido_port_t * port, const char *key)
{
    if (property_exists(port, key)) {
	hnode_t *node = hash_lookup(port->table, (const void *) key);
	if (node) {
	    livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
	    if (stor)
		return stor->num_elements;
	}
    }
    return -1;
}

/**
	int livido_property_atom_type(livido_port_t *port, const char *key)
 */
int livido_property_atom_type(livido_port_t * port, const char *key)
{
    if (property_exists(port, key)) {
	hnode_t *node = hash_lookup(port->table, (const void *) key);
	if (node) {
	    livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
	    if (stor)
		return stor->atom_type;
	}
    }
    return -1;
}

/**
	size_t livido_property_element_size (livido_port_t *port, const char *key, int idx)
 */
size_t
livido_property_element_size(livido_port_t * port, const char *key,
			     const int idx)
{
    if (property_exists(port, key)) {
	hnode_t *node = hash_lookup(port->table, (const void *) key);
	if (node) {
	    livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
#ifdef STRICT_CHECKING
	    assert(stor != NULL);
#endif
	    //todo: sum all element sizes for index of -1 
	    if (stor->num_elements == 1) {
		return stor->elements.atom->size;
	    } else if (stor->num_elements > 1) {
		/*
		   if(idx == -1)
		   {
		   int i;
		   size_t total_size = 0;
		   for( i = 0; i < stor->num_elements; i ++ )
		   total_size += stor->elements.array[i]->size;
		   return total_size;
		   } */
#ifdef STRICT_CHECKING
		assert(idx >= 0);
		assert(idx < stor->num_elements);
		assert(stor->elements.array[idx] != NULL);
#endif
		return stor->elements.array[idx]->size;
	    } else {
		if (stor->num_elements == 0)
		    return 0;
	    }
	}
    }
    return -1;

}

/**
	livido_port_t *livido_port_new (int port_type)
	livido_port_new() will set the "type" property to port_type, and will set it readonly.
 */
livido_port_t *livido_port_new(int port_type)
{
    livido_port_t *port = (livido_port_t *) malloc(sizeof(livido_port_t));
#ifdef STRICT_CHECKING
    assert(port != NULL);
#endif
    int type = port_type;

    port->table = hash_create(HASHCOUNT_T_MAX, key_compare, NULL);
#ifdef STRICT_CHECKING
    assert(port->table != NULL);
#endif
    livido_property_set(port, "type", LIVIDO_ATOM_TYPE_INT, 1, &type);
#ifdef STRICT_CHECKING
    assert(property_exists(port, "type"));
#endif
    livido_property_finalize(port, "type");

#ifdef STRICT_CHECKING
    assert(livido_property_set
	   (port, "type", LIVIDO_ATOM_TYPE_INT, 1, &type)
	   != LIVIDO_PROPERTY_READONLY);
#endif

    return port;
}

/* free the port and all its properties */

void livido_port_free(livido_port_t * port)
{
#ifdef STRICT_CHECKING
    assert( port != NULL );
    assert( port->table != NULL );
#endif

    if (port) {
#ifdef STRICT_CHECKING
	assert(port->table != NULL);
#endif
	if (!hash_isempty(port->table)) {
	    hscan_t scan;
	    hash_scan_begin(&scan, port->table);
	    hnode_t *node;

	    while ((node = hash_scan_next(&scan)) != NULL) {
		livido_storage_t *stor;
		stor = hnode_get(node);
#ifdef STRICT_CHECKING
		assert(stor != NULL);
		assert(node != NULL);
		assert((const char *) hnode_getkey(node) != NULL);
#endif
		livido_free_storage(stor);
	    }
	    hash_free_nodes(port->table);
	    hash_destroy(port->table);
	}
	free(port);
    }
    port = NULL;
}

/**
livido_property_set() will create the property if the property does not exist.

livido_property_set() will return LIVIDO_ERROR_PROPERTY_READONLY if the property has the flag bit LIVIDO_PROPERTY_READONLY set.

livido_property_set() will return an error LIVIDO_ERROR_WRONG_ATOM_TYPE if you try to change the atom_type of a property.

For livido_property_set(), num_elems can be 0 and value can then be NULL. In this way, just the atom_type of the property can be set.

The sizes field of livido_property_set() is only used for pointer type values. It should be an array of size num_elems. For fundamental types, the sizes field should be set to NULL.

pending change: size_t is known from atom_type

The void * for livido_property_set() and livido_property_get() is a (void *) typecast to/from an array of the appropriate type: e.g. for LIVIDO_ATOM_TYPE_INT it is an int *. The number of elements in the array must match num_elems in livido_property_set().
 
*/
int
livido_property_set(livido_port_t * port,
		    const char *key,
		    int atom_type, int num_elements, void *src)
{
#ifdef STRICT_CHECKING
	assert( port != NULL );
#endif
    if (property_exists(port, key)) {
	hnode_t *old_node = hash_lookup(port->table, (const void *) key);
	if (old_node) {
	    livido_storage_t *oldstor =
		(livido_storage_t *) hnode_get(old_node);
	    if (oldstor->atom_type != atom_type) {
		return LIVIDO_ERROR_WRONG_ATOM_TYPE;
	    }
	    if (oldstor->flags & LIVIDO_PROPERTY_READONLY) {
		return LIVIDO_ERROR_PROPERTY_READONLY;
	    }
	    livido_free_storage(oldstor);
	}
	hash_delete(port->table, old_node);
	hnode_destroy(old_node);
    }
    livido_storage_t *stor = livido_new_storage(num_elements);
#ifdef STRICT_CHECKING
    assert(stor != NULL);
#endif
    storage_put_atom_value(src, num_elements, stor, atom_type);

    hnode_t *node = hnode_create(stor);
#ifdef STRICT_CHECKING
    assert(node != NULL);
    assert(!hash_isfull(port->table));
    assert(!property_exists(port, key));
#endif


    hash_insert(port->table, node, (const void *) key);

    return LIVIDO_NO_ERROR;
}

/*
On livido_property_get(), Livido will copy the data stored in the property, except for pointer types. For pointer types only the reference to the memory block is copied. The programmer should first allocate a memory area for livido_property_get() to copy to.

For setting pointer types, host/plugin must pass in a size_t array with the atom sizes.

livido_property_get() will return LIVIDO_ERROR_NOSUCH_PROPERTY if a property does not exist. In this way the existence of a property can be determined. To assist with this, livido_property_get() can be called with a NULL void * value. The function will then not attempt to copy the value, but will return either LIVIDO_ERROR_NOSUCH_PROPERTY, or LIVIDO_NO_ERROR depending on whether the property exists or not.

pending change: dont pass arround size_t array, size is known from atom type

 */
int
livido_property_get(livido_port_t * port,
		    const char *key, int idx, void *dst)
{
#ifdef STRICT_CHECKING
    assert(port != NULL);
    assert(port->table != NULL);
    assert(key != NULL);
#endif
    hnode_t *node = hash_lookup(port->table, (const void *) key);
    if (!node)
	return LIVIDO_ERROR_NOSUCH_PROPERTY;

    livido_storage_t *stor = hnode_get(node);
#ifdef STRICT_CHECKING
    assert(stor != NULL);
#endif

    if (property_exists(port, key))
	return atom_get_value(stor, idx, dst);

    return LIVIDO_ERROR_NOSUCH_PROPERTY;
}

/**
  char **livido_list_properties (livido_port_t *port) // returns NULL terminated char * array of properties

 */
char **livido_list_properties(livido_port_t * port)
{
#ifdef STRICT_CHECKING
    assert(port != NULL);
    assert(port->table != NULL);
    assert(hash_isempty(port->table) == 0);
#endif
    char **list = NULL;
    int n = hash_count(port->table);

    if (n <= 0)
	return NULL;

    n++;			// null terminated list of keys

    list = (char **) malloc(sizeof(char *) * n);
    if (!list)
	return NULL;

    memset(list, 0, sizeof(char *) * n);

    hscan_t scan;
    hash_scan_begin(&scan, port->table);
    hnode_t *node;

    int i = 0;
    while ((node = hash_scan_next(&scan)) != NULL) {
	livido_storage_t *stor;
	if (node) {
	    stor = hnode_get(node);
	    const char *key = (const char *) hnode_getkey(node);
	    list[i] = (char *) strdup(key);
#ifdef STRICT_CHECKING
	    assert(list[i] != NULL);
#endif
	    i++;
	}
    }

#ifdef STRICT_CHECKING
	assert( hash_verify(port->table));
#endif
    return list;
}
