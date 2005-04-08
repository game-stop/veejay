#ifndef VJAPI_H
#define VJAPI_H

void	vj_gui_init(char *glade_file);
int	vj_gui_reconnect( char *host, char *group, int port);
void	vj_gui_free();
void	vj_fork_or_connect_veejay();
void	vj_gui_enable(void);
void	vj_gui_disable(void);
void	vj_gui_disconnect(void);
void	vj_gui_set_debug_level(int level);

#endif 
