#ifndef MULTITRACK_H
#define MULTITRACK_H


void		*multitrack_new(
		void(*f)(int,char*,int), 
		void(*g)(GdkPixbuf *, GdkPixbuf *, GtkImage *),
		GtkWidget *win,
		GtkWidget *box,
		GtkWidget *msg,
		gint w,
		gint h,
		GtkWidget *area);

void		multitrack_open( void *data );

void		multitrack_close( void *data );

int		multitrack_add_track( void *data );

void		multitrack_close_track( void *data );

void		multitrack_rule_track( void *data );

void		multitrack_set_current( void *data, char *hostname, int port_num , int w, int h);

void		multitrack_restart( void *data );

void		multitrack_sync_start(void *data);

void		multitrack_sync_simple_cmd(void *data, int vims_id, int value);

void		multitrack_quit( void *data );

void		multitrack_set_preview_speed( void *data , double value );
#endif
