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
#ifndef GTK_SEQUENCE_BANK_VIEW_H
#define GTK_SEQUENCE_BANK_VIEW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define GVR_TYPE_SEQUENCE_BANK_VIEW            (gvr_sequence_bank_view_get_type())
#define GVR_SEQUENCE_BANK_VIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GVR_TYPE_SEQUENCE_BANK_VIEW, GvrSequenceBankView))
#define GVR_IS_SEQUENCE_BANK_VIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GVR_TYPE_SEQUENCE_BANK_VIEW))
#define GVR_SEQUENCE_BANK_VIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GVR_TYPE_SEQUENCE_BANK_VIEW, GvrSequenceBankViewClass))
#define GVR_IS_SEQUENCE_BANK_VIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GVR_TYPE_SEQUENCE_BANK_VIEW))
#define GVR_SEQUENCE_BANK_VIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GVR_TYPE_SEQUENCE_BANK_VIEW, GvrSequenceBankViewClass))

typedef struct _GvrSequenceBankView GvrSequenceBankView;
typedef struct _GvrSequenceBankViewClass GvrSequenceBankViewClass;

GType      gvr_sequence_bank_view_get_type(void);
GtkWidget *gvr_sequence_bank_view_new(void);

void gvr_sequence_bank_view_set_active_bank(GtkWidget *widget, int bank);
void gvr_sequence_bank_view_set_sequence_active(GtkWidget *widget, gboolean active);
void gvr_sequence_bank_view_set_current_slot(GtkWidget *widget, int bank, int slot);
void gvr_sequence_bank_view_set_bank_revision(GtkWidget *widget, int bank, unsigned int revision);
void gvr_sequence_bank_view_set_bank_size(GtkWidget *widget, int bank, int size);
void gvr_sequence_bank_view_set_slot(GtkWidget *widget, int bank, int slot, int sample_id, int sample_type);
gboolean gvr_sequence_bank_view_get_slot(GtkWidget *widget, int bank, int slot, int *sample_id, int *sample_type);
gboolean gvr_sequence_bank_view_get_cell_at(GtkWidget *widget, int x, int y, int *bank, int *slot, gboolean *header);
void gvr_sequence_bank_view_clear_bank(GtkWidget *widget, int bank);
void gvr_sequence_bank_view_clear_all(GtkWidget *widget);
void gvr_sequence_bank_view_copy_bank(GtkWidget *widget, int src_bank, int dst_bank);
void gvr_sequence_bank_view_set_bank_clipboard(GtkWidget *widget, int bank);
gboolean gvr_sequence_bank_view_get_bank_clipboard(GtkWidget *widget, int *bank);

G_END_DECLS

#endif
