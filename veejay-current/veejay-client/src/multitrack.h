#ifndef MTRACK_H
#define MTRACK_H

#include <gtk/gtk.h>

typedef struct {
    int slot;
    int sample_id;
    int sample_type;
    int project_in;
    int project_out;
} multitrack_master_clip_t;

void *multitrack_new(
    void (*f)(int,char*,int),
    int (*g)(GdkPixbuf *, GdkPixbuf *, GtkImage *),
    GtkWidget *win,GtkWidget *box,GtkWidget *msg,GtkWidget *button,
    gint max_w,gint max_h,
    GtkWidget *main_preview_area,void *gui,int threads);
void multitrack_disconnect(void *data);
void multitrack_set_logo(void *data, GtkWidget *img);
int multitrack_add_track(void *data);
void multitrack_close_track(void *data);
void multitrack_cleanup_track(void *data, int track);
void multitrack_close_tracks(void *data);
void multitrack_set_master_track(void *data, int track);
void multitrack_set_project_master(void *data, int track);
int multrack_audoadd(void *data, char *hostname, int port_num);
int multitrack_find_track(void *data, const char *hostname, int port_num);
int multitrack_has_capacity(void *data);
int multitrack_prepare_ui_client(void *data, int track);
void *multitrack_take_ui_client(void *data, int track);
void multitrack_store_ui_client(void *data, int track, void *client);
int multitrack_get_project_master_track(void *data);
int multitrack_get_current_ui_track(void *data);
void multitrack_release_track(void *data, int id, int release_this);
void multitrack_bind_track(void *data, int id, int bind_this);
void multitrack_sync_simple_cmd2(void *data, int vims, int arg);
void *multitrack_sync(void *mt);
void multitrack_drift_update(void *data);
void multitrack_configure(void *data, float fps, int video_width, int video_height, int *bw, int *bh);
void multitrack_get_preview_dimensions(int w, int h, int *dst_w, int *dst_h);
void multitrack_update_sequence_image(void *data, int track, GdkPixbuf *img);
int update_multitrack_widgets(void *data, int *array, int track);
int multitrack_locked(void *data);
void multitrack_toggle_preview(void *data, int track_id, int status, GtkWidget *img);
void multitrack_set_quality(void *data, int quality);
void multitrack_set_shape_catalog(void *data, const char *const *names, unsigned int count);
void multitrack_refresh_connection_state(void *data);
void multitrack_sync_start(void *data);
void multitrack_sync_simple_cmd(void *data, int vims, int arg);
void multitrack_resize(void *m, int w, int h);
int mt_set_max_tracks(int tracks);
int mt_get_max_tracks(void);
int multitrack_get_track_status(void *data, int track);
int multitrack_get_sequence_view_id(void *data);

void multitrack_set_master_timeline(void *data,
                                    int bank,
                                    unsigned int revision,
                                    int total_frames,
                                    double fps,
                                    const multitrack_master_clip_t *clips,
                                    unsigned int count);
void multitrack_set_project_position(void *data,
                                     int bank,
                                     int slot,
                                     int project_frame,
                                     int active);

#endif
