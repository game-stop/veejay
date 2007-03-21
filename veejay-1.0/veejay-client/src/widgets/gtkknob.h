/* gAlan - Graphical Audio Language
 * Copyright (C) 1999 Tony Garnock-Jones
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __GTK_KNOB_H__
#define __GTK_KNOB_H__

#include <gdk/gdk.h>
#include <gtk/gtkadjustment.h>
#include <gtk/gtkwidget.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PIXMAPDIRIFY(filename) \
		(SITE_PKGDATA_DIR G_DIR_SEPARATOR_S "pixmaps" G_DIR_SEPARATOR_S filename)

#define GTK_KNOB(obj)		GTK_CHECK_CAST(obj, gtk_knob_get_type(), GtkKnob)
#define GTK_KNOB_CLASS(klass)	GTK_CHECK_CLASS_CAST(klass, gtk_knob_get_type(), GtkKnobClass)
#define GTK_IS_KNOB(obj)	GTK_CHECK_TYPE(obj, gtk_knob_get_type())

typedef struct _GtkKnob        GtkKnob;
typedef struct _GtkKnobClass   GtkKnobClass;

struct _GtkKnob {
  GtkWidget widget;

  /* update policy (GTK_UPDATE_[CONTINUOUS/DELAYED/DISCONTINUOUS]) */
  guint policy : 2;

  /* State of widget (to do with user interaction) */
  guint8 state;
  gint saved_x, saved_y;

  /* ID of update timer, or 0 if none */
  guint32 timer;

  /* Pixmap for knob */
  GdkPixbuf *pixbuf;
  /* Animation for knob... from animated gif */
  GList *anim_list;

  /* Old values from adjustment stored so we know when something changes */
  gfloat old_value;
  gfloat old_lower;
  gfloat old_upper;

  /* The adjustment object that stores the data for this knob */
  GtkAdjustment *adjustment;
  char *path;
};

struct _GtkKnobClass
{
  GtkWidgetClass parent_class;
};

extern GtkWidget *gtk_knob_new(GtkAdjustment *adjustment, const char *path);
extern guint gtk_knob_get_type(void);

extern GtkAdjustment *gtk_knob_get_adjustment(GtkKnob *knob);
extern void gtk_knob_set_update_policy(GtkKnob *knob, GtkUpdateType  policy);

extern void gtk_knob_set_adjustment(GtkKnob *knob, GtkAdjustment *adjustment);

#ifdef __cplusplus
}
#endif

#endif
