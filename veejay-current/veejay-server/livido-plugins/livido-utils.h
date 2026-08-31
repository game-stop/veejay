#ifndef LIVIDO_UTILS_H
#define LIVIDO_UTILS_H

#include "../libplugger/specs/livido.h"

int livido_default_num_threads(void);
int livido_has_property(livido_port_t *port, const char *key);
int livido_set_int_value(livido_port_t *port, const char *key, int value);
int livido_set_double_value(livido_port_t *port, const char *key, double value);
int livido_set_boolean_value(livido_port_t *port, const char *key, int value);
int livido_set_string_value(livido_port_t *port, const char *key, const char *value);
int livido_set_portptr_value(livido_port_t *port, const char *key, void *value);
int livido_set_voidptr_value(livido_port_t *port, const char *key, void *value);
int livido_get_value(livido_port_t *port, const char *key, void *value);
int livido_get_int_value(livido_port_t *port, const char *key, int *error);
double livido_get_double_value(livido_port_t *port, const char *key, int *error);
int livido_get_boolean_value(livido_port_t *port, const char *key, int *error);
char *livido_get_string_value(livido_port_t *port, const char *key, int *error);
void *livido_get_voidptr_value(livido_port_t *port, const char *key, int *error);
livido_port_t *livido_get_portptr_value(livido_port_t *port, const char *key, int *error);
int *livido_get_int_array(livido_port_t *port, const char *key, int *error);
double *livido_get_double_array(livido_port_t *port, const char *key, int *error);
int *livido_get_boolean_array(livido_port_t *port, const char *key, int *error);
char **livido_get_string_array(livido_port_t *port, const char *key, int *error);
void **livido_get_voidptr_array(livido_port_t *port, const char *key, int *error);
livido_port_t **livido_get_portptr_array(livido_port_t *port, const char *key, int *error);
int livido_set_int_array(livido_port_t *port, const char *key, int num_elems, int *values);
int livido_set_double_array(livido_port_t *port, const char *key, int num_elems, double *values);
int livido_set_boolean_array(livido_port_t *port, const char *key, int num_elems, int *values);
int livido_set_string_array(livido_port_t *port, const char *key, int num_elems, char **values);
int livido_set_voidptr_array(livido_port_t *port, const char *key, int num_elems, void **values);
int livido_set_portptr_array(livido_port_t *port, const char *key, int num_elems, livido_port_t **values);

#endif
