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
                GtkWidget *main_preview_area,
		void *gui,
		int threads,
		int max_tracks);


void		multitrack_set_logo(void *data , GtkWidget *img);

int             multitrack_add_track( void *data );

void            multitrack_close_track( void *data );

int             multrack_audoadd( void *data, char *hostname, int port_num );

void            multitrack_release_track(void *data, int id, int release_this );

void            multitrack_bind_track( void *data, int id, int bind_this );

void   	 	multitrack_sync_simple_cmd2( void *data, int vims, int arg );


void            *multitrack_sync( void * mt );

void            multitrack_configure( void *data, float fps, int video_width, int video_height, int *bw, int *bh );


void           multitrack_get_preview_dimensions( int w , int h, int *dst_w, int *dst_h );

void            multitrack_update_sequence_image( void *data , int track, GdkPixbuf *img );

int             update_multitrack_widgets( void *data, int *array, int track );

int             multitrack_locked( void *data);

void            multitrack_toggle_preview( void *data, int track_id, int status, GtkWidget *img );

void            multitrack_set_quality( void *data , int quality );

void       	multitrack_sync_start(void *data);

void		multitrack_sync_simple_cmd( void *data, int vims, int arg );

void            multitrack_resize( void *m , int w, int h );

#endif

