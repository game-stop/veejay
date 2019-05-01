/***************************************************************************
                            cellrendererspin.c
                            ------------------
    begin                : Tue Oct 21 2003
    copyright            : (C) 2003 by Tim-Philipp Müller
    email                : t.i.m at orange dot net
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

/*
 *
 *    This is a dirty 15-minute hack that tries to
 *     make editable cells with spin buttons instead
 *     of the text entry widget.
 *
 *    Modify how you please. At the moment you need
 *     to hook up your own cell data function to make
 *     sure that the number of digits is the same in
 *     editing mode as it is in non-editing mode.
 *
 *    The parameters passed to _new() should probably
 *     be properties, and probably we don't need most
 *     of them anyway. Also, it would be good if there
 *     was a better method to ensure that the number
 *     of digits is the same without this.
 *
 *    Maybe one should just rip out the whole _render
 *     stuff from GtkCellRendererText and make a
 *     whole new specialised GtkCellRenderFloat
 *     or something.
 *
 *    If anyone ever completes this code to sth useful,
 *     or achieves sth similar in another way, or has
 *     any comments on it, please drop me a mail.
 */

 /*
 * Modified by d.j.a.y , 2018
 * - gtk3 compliant
 */

#include "cellrendererspin.h"

#include <stdlib.h>

#define GUI_CELL_RENDERER_SPIN_PATH     "gui-cell-renderer-spin-path"
#define GUI_CELL_RENDERER_SPIN_INFO     "gui-cell-renderer-spin-info"

/* Some boring function declarations: GObject type system stuff */

static void       gui_cell_renderer_spin_init       (GuiCellRendererSpin      *cellspin);

static void       gui_cell_renderer_spin_class_init (GuiCellRendererSpinClass *klass);

static void       gui_cell_renderer_spin_finalize (GObject *gobject);


static gpointer   parent_class;


static GtkCellEditable
*gui_cell_renderer_spin_start_editing (GtkCellRenderer      *cell,
                                       GdkEvent             *event,
                                       GtkWidget            *widget,
                                       const gchar          *path,
                                       const GdkRectangle   *background_area,
                                       const GdkRectangle   *cell_area,
                                       GtkCellRendererState  flags);

struct _GCRSpinInfo
{
        gulong  focus_out_id;
};

typedef struct _GCRSpinInfo GCRSpinInfo;

/***************************************************************************
 *
 *  gui_cell_renderer_spin_get_type
 *
 *  Here we register our type with the GObject type system if we
 *   haven't done so yet. Everything else is done in the callbacks.
 *
 ***************************************************************************/

GType
gui_cell_renderer_spin_get_type (void)
{
  static GType cell_spin_type = 0;

  if (cell_spin_type)
    return cell_spin_type;

  if (1)
  {
    static const GTypeInfo cell_spin_info =
    {
      sizeof (GuiCellRendererSpinClass),
      NULL,                                                     /* base_init */
      NULL,                                                     /* base_finalize */
      (GClassInitFunc) gui_cell_renderer_spin_class_init,
      NULL,                                                     /* class_finalize */
      NULL,                                                     /* class_data */
      sizeof (GuiCellRendererSpin),
      0,                                                        /* n_preallocs */
      (GInstanceInitFunc) gui_cell_renderer_spin_init,
    };

    /* Derive from GtkCellRenderer */
    cell_spin_type = g_type_register_static (GTK_TYPE_CELL_RENDERER_TEXT,
                                                 "GuiCellRendererSpin",
                                                  &cell_spin_info,
                                                  0);
  }

  return cell_spin_type;
}

/***************************************************************************
 *
 *  gui_cell_renderer_spin_init
 *
 *  Set some default properties of the parent (GtkCellRendererText).
 *
 ***************************************************************************/

static void
gui_cell_renderer_spin_init (GuiCellRendererSpin *cellrendererspin)
{
        return;
}


/***************************************************************************
 *
 *  gui_cell_renderer_spin_class_init:
 *
 ***************************************************************************/

static void
gui_cell_renderer_spin_class_init (GuiCellRendererSpinClass *klass)
{
  GtkCellRendererClass *cell_class  = GTK_CELL_RENDERER_CLASS(klass);
  GObjectClass         *object_class    = G_OBJECT_CLASS(klass);

  parent_class           = g_type_class_peek_parent (klass);
  object_class->finalize = gui_cell_renderer_spin_finalize;

  /* Override the cell renderer's edit-related methods */
  cell_class->start_editing = gui_cell_renderer_spin_start_editing;
}


/***************************************************************************
 *
 *  gui_cell_renderer_spin_finalize: free any resources here
 *
 ***************************************************************************/

static void
gui_cell_renderer_spin_finalize (GObject *object)
{
/*
  GuiCellRendererSpin *cellrendererspin = GUI_CELL_RENDERER_SPIN(object);
*/

  /* Free any dynamically allocated resources here */


        /* chain up to parent class to make sure
         *  they release all their memory as well */

  (* G_OBJECT_CLASS (parent_class)->finalize) (object);
}


/***************************************************************************
 *
 *  gui_cell_renderer_spin_new
 *
 *  return a new cell renderer instance
 *   (all the parameters should really be properties)
 *
 *  Not sure which of all these values are really
 *   relevant for the spin button - needs checking!
 *
 ***************************************************************************/

GtkCellRenderer *
gui_cell_renderer_spin_new (gdouble  lower,
                            gdouble  upper,
                            gdouble  step_inc,
                            gdouble  page_inc,
                            gdouble  page_size,
                            gdouble  climb_rate,
                            guint    digits)
{
        GtkCellRenderer     *cell;
        GuiCellRendererSpin *spincell;

        cell = g_object_new(GUI_TYPE_CELL_RENDERER_SPIN, NULL);

        spincell = GUI_CELL_RENDERER_SPIN(cell);

        spincell->lower      = lower;
        spincell->upper      = upper;
        spincell->step_inc   = step_inc;
        spincell->page_inc   = page_inc;
        spincell->page_size  = page_size;
        spincell->climb_rate = climb_rate;
        spincell->digits     = digits;

  return cell;
}


/***************************************************************************
 *
 *  gui_cell_renderer_spin_editing_done
 *
 ***************************************************************************/

static void
gui_cell_renderer_spin_editing_done (GtkCellEditable *spinbutton,
                                     gpointer         data)
{
  const gchar         *path;
  const gchar         *new_text;
  GCRSpinInfo         *info;

  info = g_object_get_data (G_OBJECT (data), GUI_CELL_RENDERER_SPIN_INFO);

  if (info->focus_out_id > 0)
  {
    g_signal_handler_disconnect (spinbutton, info->focus_out_id);
    info->focus_out_id = 0;
  }

  gboolean editing_canceled;
  g_object_get (spinbutton, "editing-canceled", &editing_canceled, NULL);
  if (editing_canceled)
    return;

  path = g_object_get_data (G_OBJECT (spinbutton), GUI_CELL_RENDERER_SPIN_PATH);
  new_text = gtk_entry_get_text (GTK_ENTRY(spinbutton));

  g_signal_emit_by_name(data, "edited", path, new_text);
}


/***************************************************************************
 *
 *  gui_cell_renderer_spin_focus_out_event
 *
 ***************************************************************************/

static gboolean
gui_cell_renderer_spin_focus_out_event (GtkWidget *spinbutton,
                                        GdkEvent  *event,
                                        gpointer   data)
{
  gui_cell_renderer_spin_editing_done (GTK_CELL_EDITABLE (spinbutton), data);

  /* entry needs focus-out-event */
  return FALSE;
}

/***************************************************************************
 *
 *  gui_cell_renderer_spin_start_editing
 *
 ***************************************************************************/

static gboolean
onButtonPress (GtkWidget *spinbutton, GdkEventButton *bevent, gpointer data)
{
        if ((bevent->button == 1 && bevent->type == GDK_2BUTTON_PRESS) || bevent->type == GDK_3BUTTON_PRESS)
        {
                g_print ("double or triple click caught and ignored.\n");
                return TRUE; /* don't invoke other handlers */
        }

        return FALSE;
}


/***************************************************************************
 *
 *  gui_cell_renderer_spin_start_editing
 *
 ***************************************************************************/

static GtkCellEditable *
gui_cell_renderer_spin_start_editing (GtkCellRenderer      *cell,
                                      GdkEvent             *event,
                                      GtkWidget            *widget,
                                      const gchar          *path,
                                      const GdkRectangle   *background_area,
                                      const GdkRectangle   *cell_area,
                                      GtkCellRendererState  flags)
{
  GtkCellRendererText *celltext;
  GuiCellRendererSpin *spincell;
  GtkAdjustment       *adj;
  GtkWidget           *spinbutton;
  GCRSpinInfo         *info;
  gdouble              curval = 0.0;

  celltext = GTK_CELL_RENDERER_TEXT(cell);
  spincell = GUI_CELL_RENDERER_SPIN(cell);

  /* If the cell isn't editable we return NULL. */
  gboolean editable;
  g_object_get (celltext, "editable", &editable, NULL);
  if (editable == FALSE)
    return NULL;

  spinbutton = g_object_new (GTK_TYPE_SPIN_BUTTON, "has_frame", FALSE, "numeric", TRUE, NULL);

        /* dirty */
  gchar *text;
  g_object_get (celltext, "text", &text, NULL);
  if (text)
    curval = atof(text);

  adj = GTK_ADJUSTMENT(gtk_adjustment_new(curval,
                                          spincell->lower,
                                          spincell->upper,
                                          spincell->step_inc,
                                          spincell->page_inc,
                                          0));

        gtk_spin_button_configure(GTK_SPIN_BUTTON(spinbutton), adj, spincell->climb_rate, spincell->digits);

  g_object_set_data_full (G_OBJECT(spinbutton), GUI_CELL_RENDERER_SPIN_PATH, g_strdup (path), g_free);

  gtk_editable_select_region (GTK_EDITABLE (spinbutton), 0, -1);

  gtk_widget_show (spinbutton);

  g_signal_connect (spinbutton, "editing_done",
                          G_CALLBACK (gui_cell_renderer_spin_editing_done),
                          celltext);

        /* hack trying to catch the quite annoying effect
         *  a double click has while editing */

  g_signal_connect (spinbutton, "button_press_event",
                          G_CALLBACK (onButtonPress),
                          NULL);

        info = g_new0(GCRSpinInfo, 1);

  info->focus_out_id = g_signal_connect (spinbutton, "focus_out_event",
                             G_CALLBACK (gui_cell_renderer_spin_focus_out_event),
                             celltext);

  g_object_set_data_full (G_OBJECT (cell), GUI_CELL_RENDERER_SPIN_INFO, info, g_free);

  return GTK_CELL_EDITABLE (spinbutton);
}
