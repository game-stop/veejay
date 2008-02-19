/* vim: set ts=4 sw=4 et : */

#include <gtk/gtk.h>
#include <gtkcairo.h>

#define ANIMATE_ROTATE_STEP 0.05
#define ANIMATE_FRAME_DELAY 40

#define ROTATE_MAX 6.28         /* 2*pi */

#define MARGIN 20
#define INITIAL_SIZE 200

static void
paint (GtkWidget *widget,
       cairo_t   *cairo,
       GtkRange  *range)
{
  gint width    = widget->allocation.width;
  gint height   = widget->allocation.height;
  gint box_size = (width + height) / 6;

  cairo_save (cairo);
    cairo_identity_matrix (cairo);
    cairo_translate (cairo, width / 2, height / 2);

    cairo_rotate (cairo, gtk_range_get_value (range));
    cairo_rectangle (cairo, -box_size, -box_size, box_size, box_size);
    cairo_set_source_rgb (cairo, 1, 0, 0);
    cairo_fill (cairo);
  cairo_restore (cairo);
}

static void
slider_changed (GtkRange  *range,
                GtkWidget *gtkcairo)
{
  gtk_widget_queue_draw (gtkcairo);
}

static gboolean
animate_step (GtkRange *range)
{
  double    newval = gtk_range_get_value (range) + ANIMATE_ROTATE_STEP;
  if (newval > ROTATE_MAX)
    newval -= ROTATE_MAX;
  gtk_range_set_value (range, newval);
  return TRUE;
}

static void
animate_toggled (GtkToggleButton *tb,
                 GtkRange        *range)
{
  static guint timerid = 0;
  gboolean  active = gtk_toggle_button_get_active (tb);
  if (active && !timerid)
    {
      timerid = gtk_timeout_add (ANIMATE_FRAME_DELAY,
                                 (GtkFunction) animate_step, range);
    }
  else if (!active && timerid)
    {
      gtk_timeout_remove (timerid);
      timerid = 0;
    }
}

static void
show_test (void)
{
  GtkWidget *win, *vbox, *frame, *gtkcairo, *slider, *animate;

  win = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (win), "GtkCairo Demo");
  g_signal_connect (G_OBJECT (win), "delete-event",
                    G_CALLBACK (gtk_main_quit), NULL);

  vbox = gtk_vbox_new (FALSE, 6);
  gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);

  slider = gtk_hscale_new_with_range (0, ROTATE_MAX, 0.05);

  gtkcairo = gtk_cairo_new ();
  gtk_widget_set_usize (GTK_WIDGET (gtkcairo), INITIAL_SIZE, INITIAL_SIZE);
  g_signal_connect (G_OBJECT (gtkcairo), "paint", G_CALLBACK (paint), slider);

  gtk_container_add (GTK_CONTAINER (frame), gtkcairo);
  gtk_box_pack_start (GTK_BOX (vbox), frame, TRUE, TRUE, 0);

  gtk_scale_set_draw_value (GTK_SCALE (slider), FALSE);
  g_signal_connect (G_OBJECT (slider), "value-changed",
                    G_CALLBACK (slider_changed), gtkcairo);
  gtk_box_pack_start (GTK_BOX (vbox), slider, FALSE, FALSE, 0);

  animate = gtk_check_button_new_with_label ("Animate");
  g_signal_connect (G_OBJECT (animate), "toggled",
                    G_CALLBACK (animate_toggled), slider);
  gtk_box_pack_start (GTK_BOX (vbox), animate, FALSE, FALSE, 0);

  gtk_container_add (GTK_CONTAINER (win), vbox);
  gtk_widget_show_all (vbox);

  gtk_widget_show (win);
}

int
main (gint   argc,
      gchar *argv[])
{
  gtk_init (&argc, &argv);
  show_test ();
  gtk_main ();

  return 0;
}
