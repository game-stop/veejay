/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2002-2005 Niels Elburg <nwelburg@gmail.com>
 */

#ifndef VJCURVE_H
#define VJCURVE_H
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#define MAX_CHAIN_LEN	20

#include "gtk3curve.h"

int is_curve_empty();

int set_points_in_curve_ext(GtkWidget *curve,unsigned char *blob, int blen,int id,int fx_entry,int *curve_type,
                            int *shape, int *status, double fps);

void set_points_in_curve( Gtk3CurveType type, GtkWidget *curve);
void reset_curve( GtkWidget *curve );
void get_points_from_curve( GtkWidget *curve, int len, float *v );
void curve_param_minmax(int fx_id, int parameter_id, int *min, int *max);
void set_initial_curve( GtkWidget *curve, int fx_id, int parameter_id, int start, int end, int value, double fps );
void curve_set_position ( GtkWidget *curve, double pos);
void curve_set_predefined_shape( GtkWidget *curve, int fx_id, int parameter_id, int start, int end,
    int shape, int minb, int maxb, int steps, int seed, int detail, gboolean revers, double fps);
void gtk3_curve_set_x_timeline(GtkWidget *widget, gfloat min_x, gfloat max_x);
void gtk3_curve_get_x_timeline(GtkWidget *widget, gfloat *min_x, gfloat *max_x);

void gtk3_curve_set_x_view(GtkWidget *widget, gfloat min_x, gfloat max_x);
void gtk3_curve_get_x_view(GtkWidget *widget, gfloat *min_x, gfloat *max_x);

gboolean gtk3_curve_get_x_load_range(GtkWidget *widget, gint *start, gint *end);
gboolean gtk3_curve_is_x_zoomed(GtkWidget *widget);

void gtk3_curve_zoom_x(GtkWidget *widget, gfloat center_x, gfloat factor);
void gtk3_curve_pan_x(GtkWidget *widget, gfloat delta);
void gtk3_curve_reset_x_zoom(GtkWidget *widget);

#endif
