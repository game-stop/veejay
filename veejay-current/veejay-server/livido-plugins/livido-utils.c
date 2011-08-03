/* LiViDO is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   LiViDO is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this source code; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


   LiViDO is developed by:

   Niels Elburg - http://veejay.sf.net

   Gabriel "Salsaman" Finch - http://lives.sourceforge.net

   Denis "Jaromil" Rojo - http://freej.dyne.org

   Tom Schouten - http://zwizwa.fartit.com

   Andraz Tori - http://cvs.cinelerra.org

   reviewed with suggestions and contributions from:

   Silvano "Kysucix" Galliani - http://freej.dyne.org

   Kentaro Fukuchi - http://megaui.net/fukuchi

   Jun Iio - http://www.malib.net

   Carlo Prelz - http://www2.fluido.as:8080/

*/

/* (C) Gabriel "Salsaman" Finch, 2005 */


/////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include "../libplugger/specs/livido.h"

int livido_has_property (livido_port_t *port, const char *key) {
  if (livido_property_get(port,key,0,NULL)==LIVIDO_ERROR_NOSUCH_PROPERTY) return 0;
  return 1;
}

/////////////////////////////////////////////////////////////////
// property setters

int livido_set_int_value (livido_port_t *port, const char *key, int value) {
  // returns a LIVIDO_ERROR
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_INT,1,&value);
}

int livido_set_double_value (livido_port_t *port, const char *key, double value) {
  // returns a LIVIDO_ERROR
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_DOUBLE,1,&value);
}

int livido_set_boolean_value (livido_port_t *port, const char *key, int value) {
  // returns a LIVIDO_ERROR
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_BOOLEAN,1,&value);
}

int livido_set_string_value (livido_port_t *port, const char *key, char *value) {
  // returns a LIVIDO_ERROR
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_STRING,1,&value);
}

int livido_set_portptr_value (livido_port_t *port, const char *key, void *value) {
  // returns a LIVIDO_ERROR
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_PORTPTR,1,&value);
}

int livido_set_voidptr_value (livido_port_t *port, const char *key, void *value) {
  // returns a LIVIDO_ERROR
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_VOIDPTR,1,&value);
}


/////////// these functions need a size ////////////


//////////////////////////////////////////////////////////////////////////////////////////////////
// general property getter

inline int livido_get_value (livido_port_t *port, const char *key, void *value) {
  // returns a LIVIDO_ERROR
  return livido_property_get( port, key, 0, value);
}

////////////////////////////////////////////////////////////

int livido_get_int_value (livido_port_t *port, const char *key, int *error) {
  int retval=0;
  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_INT) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return retval;
  }
  else *error=livido_get_value (port,key,&retval);
  return retval;
}

double livido_get_double_value (livido_port_t *port, const char *key, int *error) {
  double retval=0.;
  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_DOUBLE) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return retval;
  }
  *error=livido_get_value (port,key,&retval);
  return retval;
}

int livido_get_boolean_value (livido_port_t *port, const char *key, int *error) {
  int retval=0;
  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_BOOLEAN) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return retval;
  }
  *error=livido_get_value (port,key,&retval);
  return retval;
}

char *livido_get_string_value (livido_port_t *port, const char *key, int *error) {
  char *retval=NULL;
  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_STRING) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return NULL;
  }
  if ((retval=(char *)livido_malloc(livido_property_element_size(port,key,0)+1))==NULL) {
    *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
    return NULL;
  }
  if ((*error=livido_get_value (port,key,&retval))!=LIVIDO_NO_ERROR) {
    livido_free (retval);
    return NULL;
  }
  return retval;
}

void *livido_get_voidptr_value (livido_port_t *port, const char *key, int *error) {
  void *retval=NULL;
  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_VOIDPTR) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return retval;
  }
  *error=livido_get_value (port,key,&retval);
  return retval;
}

livido_port_t *livido_get_portptr_value (livido_port_t *port, const char *key, int *error) {
  livido_port_t *retval=NULL;
  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_PORTPTR) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return retval;
  }
  *error=livido_get_value (port,key,&retval);
  return retval;
}


////////////////////////////////////////////////////////////

int *livido_get_int_array (livido_port_t *port, const char *key, int *error) {
  int i;
  int num_elems;
  int *retval;

  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_INT) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return NULL;
  }

  if ((num_elems=livido_property_num_elements (port,key))==0) return NULL;

  if ((retval=(int *)livido_malloc(num_elems*sizeof(int)))==NULL) {
    *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
    return NULL;
  }

  for (i=0;i<num_elems;i++) {
    if ((*error=livido_property_get(port, key, i, &retval[i]))!=LIVIDO_NO_ERROR) {
      livido_free (retval);
      return NULL;
    }
  }
  return retval;
}

double *livido_get_double_array (livido_port_t *port, const char *key, int *error) {
  int i;
  int num_elems;
  double *retval;

  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_DOUBLE) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return NULL;
  }
  if ((num_elems=livido_property_num_elements (port,key))==0) return NULL;

  if ((retval=(double *)livido_malloc(num_elems*sizeof(double)))==NULL) {
    *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
    return NULL;
  }

  for (i=0;i<num_elems;i++) {
    if ((*error=livido_property_get(port, key, i, &retval[i]))!=LIVIDO_NO_ERROR) {
      livido_free (retval);
      return NULL;
    }
  }
  return retval;
}

int *livido_get_boolean_array (livido_port_t *port, const char *key, int *error) {
  int i;
  int num_elems;
  int *retval;

  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_BOOLEAN) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return NULL;
  }

  if ((num_elems=livido_property_num_elements (port,key))==0) return NULL;

  if ((retval=(int *)livido_malloc(num_elems*sizeof(int)))==NULL) {
    *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
    return NULL;
  }

  for (i=0;i<num_elems;i++) {
    if ((*error=livido_property_get(port, key, i, &retval[i]))!=LIVIDO_NO_ERROR) {
      livido_free (retval);
      return NULL;
    }
  }
  return retval;
}

char **livido_get_string_array (livido_port_t *port, const char *key, int *error) {
  int i;
  int num_elems;
  char **retval;

  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_STRING) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return NULL;
  }

  if ((num_elems=livido_property_num_elements (port,key))==0) return NULL;

  if ((retval=(char **)livido_malloc(num_elems*sizeof(char *)))==NULL) {
    *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
    return NULL;
  }

  for (i=0;i<num_elems;i++) {
    if ((retval[i]=(char *)livido_malloc(livido_property_element_size(port,key,i)+1))==NULL) {
      for (--i;i>=0;i--) livido_free(retval[i]);
      *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
      livido_free (retval);
      return NULL;
    }
    if ((*error=livido_property_get(port, key, i, &retval[i]))!=LIVIDO_NO_ERROR) {
      for (--i;i>=0;i--) livido_free(retval[i]);
      livido_free (retval);
      return NULL;
    }
  }
  return retval;
}

void **livido_get_voidptr_array (livido_port_t *port, const char *key, int *error) {
  int i;
  int num_elems;
  void **retval;

  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_VOIDPTR) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return NULL;
  }

  if ((num_elems=livido_property_num_elements (port,key))==0) return NULL;

  if ((retval=(void **)livido_malloc(num_elems*sizeof(void *)))==NULL) {
    *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
    return NULL;
  }

  for (i=0;i<num_elems;i++) {
    if ((*error=livido_property_get(port, key, i, &retval[i]))!=LIVIDO_NO_ERROR) {
      livido_free (retval);
      return NULL;
    }
  }
  return retval;
}

livido_port_t **livido_get_portptr_array (livido_port_t *port, const char *key, int *error) {
  int i;
  int num_elems;
  livido_port_t **retval;

  if (livido_has_property(port,key)&&livido_property_atom_type(port,key)!=LIVIDO_ATOM_TYPE_PORTPTR) {
    *error=LIVIDO_ERROR_WRONG_ATOM_TYPE;
    return NULL;
  }

  if ((num_elems=livido_property_num_elements (port,key))==0) return NULL;

  if ((retval=(livido_port_t **)livido_malloc(num_elems*sizeof(livido_port_t *)))==NULL) {
    *error=LIVIDO_ERROR_MEMORY_ALLOCATION;
    return NULL;
  }

  for (i=0;i<num_elems;i++) {
    if ((*error=livido_property_get(port, key, i, &retval[i]))!=LIVIDO_NO_ERROR) {
      livido_free (retval);
      return NULL;
    }
  }
  return retval;
}

/////////////////////////////////////////////////////

int livido_set_int_array (livido_port_t *port, const char *key, int num_elems, int *values) {
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_INT,num_elems,values);
}

int livido_set_double_array (livido_port_t *port, const char *key, int num_elems, double *values) {
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_DOUBLE,num_elems,values);
}

int livido_set_boolean_array (livido_port_t *port, const char *key, int num_elems, int *values) {
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_BOOLEAN,num_elems,values);
}

int livido_set_string_array (livido_port_t *port, const char *key, int num_elems, char **values) {
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_STRING,num_elems,values);
}

int livido_set_voidptr_array (livido_port_t *port, const char *key, int num_elems, void **values) {
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_VOIDPTR,num_elems,values);
}

int livido_set_portptr_array (livido_port_t *port, const char *key, int num_elems, livido_port_t **values) {
  return livido_property_set (port,key,LIVIDO_ATOM_TYPE_PORTPTR,num_elems,values);
}

