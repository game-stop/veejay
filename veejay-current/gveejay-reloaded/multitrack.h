#ifndef MTRACK_H
#define MTRACK_H
        
void            *multitrack_new(
                void (*f)(int,char*,int),
                int (*g)(GdkPixbuf *, GdkPixbuf *, GtkImage *),
                GtkWidget *win,
                GtkWidget *box,
                GtkWidget *msg,
		GtkWidget *button,
                gint max_w,
                gint max_h,
                GtkWidget *main_preview_area);


int             multitrack_add_track( void *data );

void            multitrack_close_track( void *data );

int             multrack_audoadd( void *data, char *hostname, int port_num );

void            multitrack_release_track(void *data, int id, int release_this );

void            multitrack_bind_track( void *data, int id, int bind_this );

#endif

