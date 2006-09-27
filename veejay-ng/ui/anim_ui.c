#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif

#include <libvevo/libvevo.h>
#include <ui/anim.h>

typedef struct
{
	GtkWidget *win;
	void	  *curve;
} anim_ui_t;

static void	*anim_collection_ = NULL;


static	gboolean	anim_ui_delete_window(
		GtkWidget *widget, GdkEvent *event, gpointer user_data )
{
	return FALSE;
}

static	gboolean	anim_ui_destroy_window(
		GtkWidget *widget, GdkEvent *event, gpointer user_data )
{
	anim_ui_t *anim = (anim_ui_t*) user_data;
	anim_destroy( anim->curve );

	return FALSE;
}

static void	on_anim_ui_clear_clicked(
			GtkWidget *widget, gpointer user_data )
{
	anim_ui_t *anim = (anim_ui_t*) user_data;

	anim_clear( anim->curve );
}

static void	on_anim_ui_apply_clicked(
			GtkWidget *widget, gpointer user_data )
{
	anim_ui_t *anim = (anim_ui_t*) user_data;

	anim_update( anim->curve );
}

static void	on_anim_ui_type_changed(
			GtkWidget *widget, gpointer user_data )
{
	anim_ui_t *anim = (anim_ui_t*) user_data;
	gint type = gtk_combo_box_get_active( GTK_COMBO_BOX( widget ));
	
	anim_change_curve( anim->curve, type );
}

static	void	anim_ui_destroy( void *danim )
{
	anim_ui_t *anim = (anim_ui_t*) danim;
	vevo_property_set( anim_collection_, anim_get_path(anim->curve),VEVO_ATOM_TYPE_VOIDPTR, 0, NULL );
	gtk_widget_destroy( anim->win );
}

static void	anim_ui_bang(void *danim, double pos)
{
	anim_ui_t *anim = (anim_ui_t*) danim;
	anim_bang( danim, pos );	
}

void	anim_ui_collection_bang( double pos )
{
	char **anims = vevo_list_properties( anim_collection_ );
	if(!anims)
		return;

	int i;
	for( i = 0; anims[i] != NULL ; i ++ )
	{
		void *anim = NULL;
		if( vevo_property_get( anim_collection_ , anims[i],0, &anim ) == VEVO_NO_ERROR )
			anim_ui_bang( anim , pos);
		free(anims[i]);
	}
	free(anims);
}

void	anim_ui_collection_free( )
{
	char **anims = vevo_list_properties( anim_collection_ );
	if(!anims)
		return;

	int i;
	for( i = 0; anims[i] != NULL ; i ++ )
	{
		void *anim = NULL;
		if( vevo_property_get( anim_collection_ , anims[i],0, &anim ) == VEVO_NO_ERROR )
			anim_ui_destroy( anim );
		free(anims[i]);
	}
	free(anims);
}

void	anim_ui_collection_destroy()
{
	vevo_port_free( anim_collection_);
}

void	anim_ui_collection_init()
{
	anim_collection_ = vpn( VEVO_ANONYMOUS_PORT );
}

void	anim_ui_new(void *sender, char *path, char *types)
{
	anim_ui_t *anim = (anim_ui_t*) malloc(sizeof(anim_ui_t));
	anim->win = gtk_window_new( GTK_WINDOW_TOPLEVEL );
	gtk_window_set_title( anim->win, path );
	gtk_window_set_resizable( anim->win, TRUE );
	
	g_signal_connect( G_OBJECT(anim->win), "delete_event",
				G_CALLBACK( anim_ui_delete_window ), NULL );
	g_signal_connect( G_OBJECT(anim->win), "destroy",
				G_CALLBACK( anim_ui_destroy_window ), NULL );

	gtk_container_set_border_width( GTK_CONTAINER( anim->win ), 1 );

	GtkWidget *box2 = gtk_vbox_new( FALSE, 0 );
	gtk_container_add( GTK_CONTAINER( anim->win ), box2 );


	GtkWidget *box = gtk_hbox_new( FALSE, 0 );
	gtk_box_pack_end( box, box2, FALSE,FALSE, 0);


	GtkWidget *but = gtk_button_new_with_label( "Clear" );
	g_signal_connect( but, "clicked",
		(GCallback) on_anim_ui_clear_clicked, (gpointer) anim );
	gtk_box_pack_end( box, but, FALSE,FALSE, 0);

	but = gtk_button_new_with_label( "Apply" );
	g_signal_connect( but, "clicked",
		(GCallback) on_anim_ui_apply_clicked, (gpointer) anim );
	gtk_box_pack_end( box, but, FALSE,FALSE, 0);

	but = gtk_combo_box_new_text( );
        GtkTreeModel *model = gtk_combo_box_get_model( GTK_COMBO_BOX(but) );
        gtk_list_store_clear( GTK_LIST_STORE( model ) );
	
	gtk_combo_box_append_text( GTK_COMBO_BOX( but ),
					"Free Hand" );
	gtk_combo_box_append_text( GTK_COMBO_BOX( but ),
					"Linear" );
	gtk_combo_box_append_text( GTK_COMBO_BOX( but ),
					"Spline" );
	gtk_combo_box_set_active( GTK_COMBO_BOX( but ), 0 );

	g_signal_connect( but, "changed",
		(GCallback) on_anim_ui_type_changed, (gpointer) anim );
	gtk_box_pack_end( box, but, FALSE,FALSE, 0);


	anim->curve = anim_new( sender, path, types );
	GtkWidget *pad = (GtkWidget*) anim_get( anim->curve );
	gtk_box_pack_end( box2, anim->curve, FALSE,FALSE, 0);


	gtk_widget_show_all(box);
	gtk_widget_show_all(box2);	

	vevo_property_set( anim_collection_,
				path,
				VEVO_ATOM_TYPE_VOIDPTR,
				1,
				&anim );

	gtk_widget_show_all( anim->win );
}

