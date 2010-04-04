#ifndef PLUGDEFS_H
#define PLUGDEFS_H
#define	VEVO_PLUG_LIVIDO	0xffaa
#define	VEVO_PLUG_FF		0x00ff
#define VEVO_PLUG_FR		0xffbb
#ifndef HAVE_LIVIDO_PORT_T
#define HAVE_LIVIDO_PORT_T
typedef   vevo_port_t livido_port_t;
#endif

typedef	  void	(*generic_process_f)(void *instance, double timecode);
typedef	  void	(*generic_push_channel_f)(void *instance, int seq_num, int dir, VJFrame *frame);
typedef	  void	(*generic_default_values_f)(void *instance, void *fx_values);
typedef	  void	(*generic_push_parameter_f)(void *instance, int seq_num, void *value );
typedef	  void  (*generic_clone_parameter_f)(void *instance, int seq_num, void *fx_values );
typedef	  void	(*generic_reverse_clone_parameter_f)(void *instance, int seq_num, void *fxvalues );
typedef	  int	(*generic_reverse_clone_out_parameter_f)(void *instance, void *fxvalues );
typedef	  void	(*generic_deinit_f)(void *instance);
typedef	  void	(*generic_init_f)(void *instance, int w, int h );
typedef	  void  (*generic_osc_cb_f)(void *userdata, void *param, void *value );

extern	int livido_property_num_elements(livido_port_t * p, const char *key);
extern	int livido_property_atom_type(livido_port_t * p, const char *key);
extern	size_t livido_property_element_size(livido_port_t * p, const char *key,const int idx);
extern	livido_port_t *livido_port_new(int port_type);
extern	void livido_port_free(livido_port_t * p);
extern  int livido_property_set(livido_port_t * p,const char *key,int atom_type, int num_elements, void *src);
extern 	int livido_property_get(livido_port_t * p, const char *key, int idx, void *dst);
extern	char **livido_list_properties(livido_port_t * p);





#define livido_port_free	vevo_port_free
#define livido_port_new		vpn
#define livido_property_set	vevo_property_set
#define livido_property_get	vevo_property_get
#define	livido_property_element_size vevo_property_element_size
#define livido_property_num_elements vevo_property_num_elements
#define livido_property_atom_type vevo_property_atom_type
#define livido_list_properties vevo_list_properties
//veejay only
#define livido_port_recursive_free vevo_port_recursive_free
#define livido_dump_port	vevo_port_dump
#define	livido_property_soft_reference	vevo_property_soft_reference
#endif
