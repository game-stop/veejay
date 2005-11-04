#define _GNU_SOURCE
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <include/vevo.h>
#include <include/livido.h>
#include <include/hash.h>
/**
	VeVo's mediation layer and core livido functions implementation
	using hashtable, see hash.h	
 */

#ifdef STRICT_CHECKING
#include <assert.h>
#endif

/* Mediation layer structures, provided by this host */
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

/* For fast indexing of livido keys, define the port index: */
typedef struct {
    const char *key;
    int hash_code;
    void *next;
} port_index_t;

static inline port_index_t *port_node_new(const char *key, int hash_key)
{
    port_index_t *i = (port_index_t *) malloc(sizeof(port_index_t));
    i->key = strdup(key);
    i->hash_code = hash_key;
    i->next = NULL;
    return i;
}
static inline void port_node_free(port_index_t * node)
{
    if (node) {
	if (node->key)
	    free((void *) node->key);
	free(node);
    }
    node = NULL;
}
static inline void port_node_append(livido_port_t * port, const char *key,
				    int hash_key)
{
    port_index_t *node = port_node_new(key, hash_key);
    port_index_t *next;
    port_index_t *list = (port_index_t *) port->index;
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

/* forward declarations */
#define property_exists( port, key ) hash_lookup( (hash_t*) port->table, (const void*) key )

#define atom_store__(value) {\
for (i = 0; i < d->num_elements; i++)\
 d->elements.array[i] = livido_put_atom( &value[i], v ); }

#define array_load__(value) {\
for( i = 0; i < t->num_elements ; i ++ )\
 memcpy( &value[i], t->elements.array[i]->value, t->elements.array[i]->size ); }


/* fast key hashing */
static inline int hash_key_code(const char *key)
{
    int hash = 0;
    while (*key) {
	hash <<= 1;
	if (hash < 0)
	    hash |= 1;
	hash ^= *key++;
    }
    return hash;
}
static int livido_property_finalize(livido_port_t * port, const char *key)
{
    hnode_t *node = NULL;
    int hash_key = hash_key_code(key);

    if ((node = property_exists(port, hash_key)) != NULL) {
	livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
	stor->flags |= LIVIDO_PROPERTY_READONLY;
	hnode_t *new_node = hnode_create((void *) stor);
	hnode_put(new_node, (void *) hash_key);
	hnode_destroy(new_node);
    }

    return LIVIDO_NO_ERROR;
}

static int atom_get_value(livido_storage_t * t, int idx, void *dst)
{
#ifdef STRICT_CHECKING
    assert(t != NULL);
#endif
    atom_t *atom = NULL;

    if (t->num_elements == 1 && idx == 0)
	atom = t->elements.atom;

    if (t->num_elements > 1 && idx >= 0 && idx <= t->num_elements)
	atom = t->elements.array[idx];

    if (!atom)
	return LIVIDO_ERROR_NOSUCH_ELEMENT;

    if (atom->size <= 0)
	return LIVIDO_NO_ERROR;


    if (t->atom_type != LIVIDO_ATOM_TYPE_STRING) {
	memcpy(dst, atom->value, atom->size);
    } else {
	char **ptr = (char **) dst;
	char *p = *ptr;
	memcpy(p, atom->value, (atom->size - 1));
	p[atom->size - 1] = '\0';
    }
    return LIVIDO_NO_ERROR;
}


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

static atom_t *livido_new_atom(int atom_type, size_t atom_size)
{
    atom_t *atom = (atom_t *) malloc(sizeof(atom_t));
#ifdef STRICT_CHECKING
    assert(atom != NULL);
#endif
    atom->size = atom_size;
    atom->value = (atom_size > 0 ? (void *) malloc(atom_size) : NULL);
#ifdef STRICT_CHECING
    assert(atom->value != NULL);
#endif
    return atom;
}

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

static atom_t *livido_put_atom(void *dst, int atom_type)
{
    atom_t *atom = NULL;
    size_t atom_size = livido_atom_size(atom_type);
    if (atom_type == LIVIDO_ATOM_TYPE_STRING) {
	char **s = (char **) dst;
	atom_size = strlen(*s) + 1;
	atom = livido_new_atom(atom_type, atom_size);
	if (atom_size > 0)
	    memcpy(atom->value, *s, (atom_size - 1));
    } else {
#ifdef STRICT_CHECKING
	assert(atom_size > 0);
	assert(dst != NULL);
#endif
	atom = livido_new_atom(atom_type, atom_size);
#ifdef STRICT_CHECING
	assert(atom != NULL);
#endif
	memcpy(atom->value, dst, atom_size);
    }
    return atom;
}

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

    switch (n) {
    case 0:
	d->elements.atom = livido_new_atom(v, livido_atom_size(v));
	break;
    case 1:
	d->elements.atom = livido_put_atom(src, v);
	break;
    default:
	d->elements.array = (atom_t **) malloc(sizeof(atom_t *) * n);
	if (d->atom_type == LIVIDO_ATOM_TYPE_DOUBLE) {
	    double *value = (double *) src;
	    atom_store__(value);
	} else {
	    if (d->atom_type == LIVIDO_ATOM_TYPE_INT
		|| d->atom_type == LIVIDO_ATOM_TYPE_BOOLEAN) {
		int32_t *value = (int *) src;
		atom_store__(value);
	    } else {
		void **value = (void **) src;
		atom_store__(value);
	    }
	}
	break;
    }
}

static inline livido_storage_t *livido_new_storage(int num_elements)
{
    livido_storage_t *d =
	(livido_storage_t *) malloc(sizeof(livido_storage_t));
    d->elements.atom = NULL;
    d->num_elements = num_elements;
    d->flags = 0;
    return d;
}

static inline void livido_free_storage(livido_storage_t * t)
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
static inline int reversestrcmp(const char *key1, const char *key2)
{
    int k = strlen(key2);
    int n = strlen(key1);
    if (k < n)
	n = k;
    key1 += n - 1;
    key2 += n - 1;
    while (n-- > 0) {
	if (*key1 != *key2) {
	    return 1;
	}
	key1--;
	key2--;
    }
    if (n < 0)
	return 0;
    return 1;
}


static inline hash_val_t int_hash(const void *key)
{
    return (hash_val_t) key;
}

static inline int key_compare(const void *key1, const void *key2)
{
    return ((int) key1 == (int) key2 ? 0 : 1);
}

int livido_property_num_elements(livido_port_t * port, const char *key)
{
    hnode_t *node = NULL;
    int hash_key = hash_key_code(key);

    if ((node = property_exists(port, hash_key)) != NULL) {
	livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
	if (stor)
	    return stor->num_elements;
    }
    return -1;
}

int livido_property_atom_type(livido_port_t * port, const char *key)
{
    hnode_t *node = NULL;
    int hash_key = hash_key_code(key);
    if ((node = property_exists(port, hash_key)) != NULL) {
	livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
	if (stor)
	    return stor->atom_type;
    }
    return -1;
}

size_t
livido_property_element_size(livido_port_t * port, const char *key,
			     const int idx)
{
    hnode_t *node = NULL;
    int hash_key = hash_key_code(key);

    if ((node = property_exists(port, hash_key)) != NULL) {
	livido_storage_t *stor = (livido_storage_t *) hnode_get(node);
#ifdef STRICT_CHECKING
	assert(stor != NULL);
#endif
	//todo: sum all element sizes for index of -1 
	if (stor->num_elements == 1) {
	    return stor->elements.atom->size;
	} else if (stor->num_elements > 1) {
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
    return -1;
}

livido_port_t *livido_port_new(int port_type)
{
    livido_port_t *port = (livido_port_t *) malloc(sizeof(livido_port_t));
#ifdef STRICT_CHECKING
    assert(port != NULL);
#endif
    int type = port_type;

    port->table =
	(void *) hash_create(HASHCOUNT_T_MAX, key_compare, int_hash);
    port->index = NULL;
#ifdef STRICT_CHECKING
    assert(port->table != NULL);
#endif
    livido_property_set(port, "type", LIVIDO_ATOM_TYPE_INT, 1, &type);
#ifdef STRICT_CHECKING
    int hash_key = hash_key_code("type");
    assert(property_exists(port, hash_key) != NULL);
#endif
    livido_property_finalize(port, "type");

#ifdef STRICT_CHECKING
    assert(livido_property_set
	   (port, "type", LIVIDO_ATOM_TYPE_INT, 1, &type)
	   != LIVIDO_PROPERTY_READONLY);
#endif

    return port;
}

void livido_port_free(livido_port_t * port)
{
#ifdef STRICT_CHECKING
    assert(port != NULL);
    assert(port->table != NULL);
#endif

    if (port) {
#ifdef STRICT_CHECKING
	assert(port->table != NULL);
#endif
	if (!hash_isempty((hash_t *) port->table)) {
	    hscan_t scan;
	    hash_scan_begin(&scan, (hash_t *) port->table);
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
	    hash_free_nodes((hash_t *) port->table);
	    hash_destroy((hash_t *) port->table);
	}

	if (port->index) {
	    port_index_t *l = (port_index_t *) port->index;
	    port_index_t *n = NULL;
	    while (l != NULL) {
		n = l->next;
		port_node_free(l);
		l = n;
	    }
	}
	free(port);
    }
    port = NULL;
}

int
livido_property_set(livido_port_t * port,
		    const char *key,
		    int atom_type, int num_elements, void *src)
{
#ifdef STRICT_CHECKING
    assert(port != NULL);
#endif
    hnode_t *old_node = NULL;
    int hash_key = hash_key_code(key);
    if ((old_node = property_exists(port, hash_key)) != NULL) {
	livido_storage_t *oldstor =
	    (livido_storage_t *) hnode_get(old_node);
	if (oldstor->atom_type != atom_type)
	    return LIVIDO_ERROR_WRONG_ATOM_TYPE;

	if (oldstor->flags & LIVIDO_PROPERTY_READONLY)
	    return LIVIDO_ERROR_PROPERTY_READONLY;

	livido_free_storage(oldstor);

	hash_delete((hash_t *) port->table, old_node);
	hnode_destroy(old_node);
    } else {
	port_node_append(port, key, hash_key);
    }
    livido_storage_t *stor = livido_new_storage(num_elements);
#ifdef STRICT_CHECKING
    assert(stor != NULL);
#endif
    storage_put_atom_value(src, num_elements, stor, atom_type);

    hnode_t *node = hnode_create(stor);
#ifdef STRICT_CHECKING
    assert(node != NULL);
    assert(!hash_isfull((hash_t *) port->table));
    assert(!property_exists(port, hash_key));
#endif
    hash_insert((hash_t *) port->table, node, (const void *) hash_key);

    return LIVIDO_NO_ERROR;
}

int
livido_property_get(livido_port_t * port,
		    const char *key, int idx, void *dst)
{
#ifdef STRICT_CHECKING
    assert(port != NULL);
    assert(port->table != NULL);
    assert(key != NULL);
#endif
    hnode_t *node = NULL;
    int hash_key = hash_key_code(key);
    if ((node = property_exists(port, hash_key)) != NULL) {
	if (dst == NULL)
	    return LIVIDO_NO_ERROR;
	else {
	    livido_storage_t *stor = hnode_get(node);
#ifdef STRICT_CHECKING
	    assert(stor != NULL);
#endif
	    return atom_get_value(stor, idx, dst);
	}
    }
    return LIVIDO_ERROR_NOSUCH_PROPERTY;
}

char **livido_list_properties(livido_port_t * port)
{
#ifdef STRICT_CHECKING
    assert(port != NULL);
    assert(port->table != NULL);
    assert(hash_isempty((hash_t *) port->table) == 0);
#endif
    char **list = NULL;
#ifdef STRICT_CHECKING
    int nn = hash_count((hash_t *) port->table);
#endif
    int n = 1;			// null terminated list of keys
    int i = 0;
    port_index_t *l = (port_index_t *) port->index;
    while (l != NULL) {
	l = l->next;
	n++;
    }
#ifdef STRICT_CHECKING
    assert(nn == n);
#endif

    list = (char **) malloc(sizeof(char *) * n);
    if (!list)
	return NULL;

    l = (port_index_t *) port->index;
    i = 0;
    while (l != NULL) {
	list[i] = (char *) strdup(l->key);
#ifdef STRICT_CHECING
	assert(list[i] != NULL);
#endif
	l = l->next;
	i++;
    }
    list[i] = NULL;

    return list;
}
