/* Gveejay Reloaded - graphical interface for VeeJay
 * 	     (C) 2026 Niels Elburg <nwelburg@gmail.com> 
 *
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#ifndef GTK_SAMPLE_BANK_VIEW_H
#define GTK_SAMPLE_BANK_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_SAMPLE_BANK_VIEW            (gvr_sample_bank_view_get_type())
#define GVR_SAMPLE_BANK_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_SAMPLE_BANK_VIEW, GvrSampleBankView))
#define GVR_IS_SAMPLE_BANK_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_SAMPLE_BANK_VIEW))
#define GVR_SAMPLE_BANK_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_SAMPLE_BANK_VIEW, GvrSampleBankViewClass))
#define GVR_IS_SAMPLE_BANK_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_SAMPLE_BANK_VIEW))
#define GVR_SAMPLE_BANK_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_SAMPLE_BANK_VIEW, GvrSampleBankViewClass))

typedef struct _GvrSampleBankView GvrSampleBankView;
typedef struct _GvrSampleBankViewClass GvrSampleBankViewClass;

GType      gvr_sample_bank_view_get_type(void);
GtkWidget *gvr_sample_bank_view_new(void);

void gvr_sample_bank_view_set_layout(GtkWidget *widget, int columns, int rows);
void gvr_sample_bank_view_set_page_count(GtkWidget *widget, int pages);
int  gvr_sample_bank_view_get_page_count(GtkWidget *widget);
void gvr_sample_bank_view_set_current_page(GtkWidget *widget, int page);
int  gvr_sample_bank_view_get_current_page(GtkWidget *widget);
void gvr_sample_bank_view_step_page(GtkWidget *widget, int delta);

void gvr_sample_bank_view_set_slot(GtkWidget *widget,
                                   int page,
                                   int slot,
                                   int sample_id,
                                   int sample_type,
                                   const char *title,
                                   const char *timecode);
void gvr_sample_bank_view_clear_slot(GtkWidget *widget, int page, int slot);
void gvr_sample_bank_view_clear_all(GtkWidget *widget);
void gvr_sample_bank_view_set_thumbnail(GtkWidget *widget, int page, int slot, GdkPixbuf *pixbuf);
void gvr_sample_bank_view_set_selected_slot(GtkWidget *widget, int page, int slot);
void gvr_sample_bank_view_set_current_source(GtkWidget *widget, int sample_id, int sample_type);
gboolean gvr_sample_bank_view_get_slot(GtkWidget *widget,
                                        int page,
                                        int slot,
                                        int *sample_id,
                                        int *sample_type);
gboolean gvr_sample_bank_view_get_cell_at(GtkWidget *widget,
                                           int x,
                                           int y,
                                           int *page,
                                           int *slot,
                                           gboolean *header);

G_END_DECLS

#endif
