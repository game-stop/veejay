/***************************************************************************
                            cellrendererspin.h
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

#ifndef _cellrendererspin_h_included_
#define _cellrendererspin_h_included_

#include <gtk/gtk.h>

/* Some boilerplate GObject type check and type cast macros */

#define GUI_TYPE_CELL_RENDERER_SPIN             (gui_cell_renderer_spin_get_type())
#define GUI_CELL_RENDERER_SPIN(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),  GUI_TYPE_CELL_RENDERER_SPIN, GuiCellRendererSpin))
#define GUI_CELL_RENDERER_SPIN_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass),  GUI_TYPE_CELL_RENDERER_SPIN, GuiCellRendererSpinClass))
#define GUI_IS_CELL_RENDERER_SPIN(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GUI_TYPE_CELL_RENDERER_SPIN))
#define GUI_IS_CELL_RENDERER_SPIN_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass),  GUI_TYPE_CELL_RENDERER_SPIN))
#define GUI_CELL_RENDERER_SPIN_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj),  GUI_TYPE_CELL_RENDERER_SPIN, GuiCellRendererSpinClass))



typedef struct _GuiCellRendererSpin      GuiCellRendererSpin;
typedef struct _GuiCellRendererSpinClass GuiCellRendererSpinClass;


struct _GuiCellRendererSpin
{
  GtkCellRendererText   parent;

        gdouble               lower;
        gdouble               upper;
        gdouble               step_inc;
        gdouble               page_inc;
        gdouble               page_size;
        gdouble               climb_rate;
        guint                 digits;
};

struct _GuiCellRendererSpinClass
{
  GtkCellRendererTextClass  parent_class;
};

GType                gui_cell_renderer_spin_get_type (void);

GtkCellRenderer     *gui_cell_renderer_spin_new (gdouble  lower,
                                                 gdouble  upper,
                                                 gdouble  step_inc,
                                                 gdouble  page_inc,
                                                 gdouble  page_size,
                                                 gdouble  climb_rate,
                                                 guint    digits);


#endif /* _spinbar_renderer_h_included_ */

