/* veejay - Linux VeeJay
 *           (C) 2002-2006 Niels Elburg <nelburg@looze.net> 
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
#include <config.h>
#include <string.h>
#include <gtk/gtk.h>
#ifdef STRICT_CHECKING
#include <assert.h>
#endif
#include <libvevo/libvevo.h>

#ifdef STRICT_CHECKING
#define vpn(type) vevo_port_new( type, __FUNCTION__ , __LINE__ )
#else
#define vpn(type) vevo_port_new( type )
#endif

typedef struct
{
	GtkLabel	*title;
	GtkLabel	*timecode;
	GtkWidget	*image;
	GtkFrame	*frame;
	GtkWidget	*event_box;
	GtkWidget	*vbox;
	int		 id;
	int		 x;
	int		 y;
	char		*path;
} slot_ui_t;


//static	void	*samplebanks_ = NULL;
static  gint num_columns_ = 0;
static  gint num_rows_    = 0;
static  slot_ui_t      **samplebank_slots_ = NULL;
static  int	available_slots_ = 0;
static  int	n_pages_     = 0;

static slot_ui_t	*samplebank_add_slot( const char *title , int x, int y);
static	gboolean	slot_clicked( GtkWidget *w, GdkEventButton *ev, gpointer data );
static slot_ui_t	*channelbank_add_slot( const char *title , int x, int y);
static	gboolean	channel_clicked( GtkWidget *w, GdkEventButton *ev, gpointer data );
static	void	*sender_ = NULL;


void *samplebank_new( void *sender, gint columns, gint rows )
{
	//samplebanks_ = vpn( VEVO_ANONYMOUS_PORT );

	GtkWidget *notebook = gtk_notebook_new();
	gtk_notebook_set_tab_pos( GTK_NOTEBOOK(notebook), GTK_POS_TOP );
	gtk_notebook_set_show_border( GTK_NOTEBOOK(notebook) , FALSE );
	gtk_notebook_popup_enable(GTK_NOTEBOOK(notebook));
	gtk_notebook_set_homogeneous_tabs( GTK_NOTEBOOK(notebook), TRUE );


	num_columns_ = columns;
	num_rows_    = rows;

	samplebank_slots_ = (slot_ui_t**) malloc(sizeof(slot_ui_t*) * 500 );
	memset( samplebank_slots_,0, sizeof(slot_ui_t*) * 500 );

	sender_ = sender;
	
	return (void*)notebook;
}


typedef struct
{
	slot_ui_t **ch;
	GtkWidget *container;
	gint	   n_slots;
} channels_t;

void *channelbank_new( void *sender, char *path, gint columns, const char **titles )
{
	gint rows = 1;
	channels_t *res = (channels_t*) malloc(sizeof(channels_t));
#ifdef STRICT_CHECKING
	assert( columns > 0 && columns < 16);
#endif
	res->container = gtk_frame_new("Input Channels");
	gtk_frame_set_shadow_type( res->container, GTK_SHADOW_ETCHED_IN );

	res->ch = (slot_ui_t**) malloc(	sizeof(slot_ui_t) * columns );
	memset( res->ch,0,sizeof(slot_ui_t*) * columns );

	GtkWidget *table = gtk_table_new( rows, columns, TRUE );
	gtk_container_add( GTK_CONTAINER( res->container ), table );

	gint i=0,j=0;
	
	for( i = 0; i < columns ; i ++ )
	{
		slot_ui_t *slot = channelbank_add_slot( titles[i],i,j );
		slot->path = strdup( path );
		gtk_table_attach_defaults( table, slot->event_box, i,i+1,j,j+1 );
		res->ch[ i ] = slot;
		res->n_slots ++;
	}

	gtk_widget_show_all( res->container );

	return (void*) res;
}

GtkWidget	*channelbank_get_container(void *cb)
{
	channels_t *ch = (channels_t*) cb;
	return ch->container;
}

void		channelbank_free(void *cb)
{
	channels_t *res = (channels_t*) cb;
	if(!res)
		return;
	gint i;
	for ( i = 0; i < res->n_slots; res ++ )
	{
		slot_ui_t *s = res->ch[i];
		if(s->path) free(s->path);
		free(s);
	}
	free(res->ch);
	free(res);
}


void	samplebank_add_page( GtkWidget *notebook )
{
	char label[128];
	char key[32];
//	void *page = vpn( VEVO_ANONYMOUS_PORT );

	sprintf(key, "page%d", n_pages_ );
//	n_pages_ ++;

	//@ store as void, we need to release pictures
/*	int error = vevo_property_set( samplebanks_, key, VEVO_ATOM_TYPE_VOIDPTR, 1, &page );
*/
	sprintf(label, "Slots %d to %d", (n_pages_ * 12), (n_pages_ * 12 ) + 11 );
	GtkWidget *frame = gtk_frame_new( NULL );
	GtkWidget *glabel = gtk_label_new( label );
	gtk_notebook_append_page( GTK_NOTEBOOK( notebook ) , frame , glabel );

	GtkWidget *table = gtk_table_new( num_rows_,num_columns_, TRUE );
	gtk_container_add( GTK_CONTAINER( frame ), table );

	gint i,j;
	for( i = 0; i < num_columns_ ; i ++ )
	{
		for( j = 0; j < num_rows_ ; j ++ )
		{
			char title[128];
			sprintf(title,"Slot %dx%d",i,j);
			slot_ui_t *slot = samplebank_add_slot( title,i,j );
			gtk_table_attach_defaults( table, slot->event_box, i,i+1,j,j+1 );
			samplebank_slots_[ available_slots_ ] = slot;
			available_slots_ ++;
		}
	}

	gtk_widget_show_all( frame );
		
	n_pages_ ++;
}

static	int	last_sample_clicked_ = 0;

static	gboolean	slot_clicked( GtkWidget *w, GdkEventButton *ev, gpointer data )
{
	slot_ui_t	*slot = (slot_ui_t*) data;

	if( ev->type == GDK_2BUTTON_PRESS )
	{
		veejay_msg(1,"play slot %d x %d, sample %d\n", slot->x, slot->y,
		     slot->id );
		ui_send_osc_( sender_, "/veejay/select", "i", slot->id );
	} else if( ev->type == GDK_BUTTON_PRESS )
	{
		veejay_msg(1,"select slot %d x %d, sample %d\n", slot->x, slot->y,
				slot->id);
		last_sample_clicked_ = slot->id;
	}	
	return FALSE;
}

static	gboolean	channel_clicked( GtkWidget *w, GdkEventButton *ev, gpointer data )
{
	slot_ui_t	*slot = (slot_ui_t*) data;

	if( ev->type == GDK_BUTTON_PRESS )
	{
		if(last_sample_clicked_ > 0 )
		{
			ui_send_osc_( sender_, slot->path, "ii", slot->x , last_sample_clicked_);
			char name[32];
			sprintf(name, "S%d", last_sample_clicked_ );
			gtk_label_set_text( slot->title, name );
		}
	} 
	return FALSE;
}





static slot_ui_t	*samplebank_add_slot( const char *title , int x, int y)
{
	slot_ui_t	*slot = (slot_ui_t*) malloc(sizeof(slot_ui_t));
	memset(	slot,0,sizeof(slot_ui_t));

	slot->event_box       = gtk_event_box_new();
	slot->x		      = x;
	slot->y               = y;

	gtk_event_box_set_visible_window( slot->event_box, TRUE );

	GTK_WIDGET_SET_FLAGS( slot->event_box, GTK_CAN_FOCUS );

	g_signal_connect( G_OBJECT( slot->event_box ),
			"button_press_event",
			G_CALLBACK( slot_clicked ),
			(gpointer) slot );

	gtk_widget_show( GTK_WIDGET( slot->event_box ) );

	slot->frame = gtk_frame_new(NULL);
	gtk_container_set_border_width( GTK_CONTAINER( slot->frame ), 0 );
	gtk_container_add( GTK_CONTAINER( slot->event_box ) , slot->frame );

	slot->vbox = gtk_vbox_new( FALSE, 0 );
	gtk_container_add( GTK_CONTAINER( slot->frame ), slot->vbox );

//	slot->image = gtk_drawing_area_new();
//	gtk_box_pack_start( GTK_BOX( slot->vbox ), GTK_WIDGET( slot->image ), TRUE,TRUE, 0 );

	gtk_widget_set_size_request( slot->frame, 32,32 );

	/*g_signal_connect( slot->image, "expose_event",
				G_CALLBACK( expose_event ),
				(gpointer) slot );*/

//	gtk_widget_show( GTK_WIDGET( slot->image ) );


	slot->title = gtk_label_new("");
	
//	slot->timecode = gtk_label_new("");

	gtk_box_pack_start( GTK_BOX( slot->vbox ), GTK_WIDGET( slot->title ), FALSE, FALSE, 0 );
//	gtk_box_pack_start( GTK_BOX( slot->vbox ), GTK_WIDGET( slot->timecode ), FALSE, FALSE, 0 );

	gtk_widget_show_all( slot->frame );
	return slot;
}

static slot_ui_t	*channelbank_add_slot( const char *title , int x, int y)
{
	slot_ui_t	*slot = (slot_ui_t*) malloc(sizeof(slot_ui_t));
	memset(	slot,0,sizeof(slot_ui_t));

	slot->event_box       = gtk_event_box_new();
	slot->x		      = x;
	slot->y               = y;

	gtk_event_box_set_visible_window( slot->event_box, TRUE );

	GTK_WIDGET_SET_FLAGS( slot->event_box, GTK_CAN_FOCUS );

	g_signal_connect( G_OBJECT( slot->event_box ),
			"button_press_event",
			G_CALLBACK( channel_clicked ),
			(gpointer) slot );

	gtk_widget_show( GTK_WIDGET( slot->event_box ) );

	slot->frame = gtk_frame_new(NULL);
	gtk_container_set_border_width( GTK_CONTAINER( slot->frame ), 1 );
	gtk_container_add( GTK_CONTAINER( slot->event_box ) , slot->frame );

	slot->vbox = gtk_vbox_new( FALSE, 0 );
	gtk_container_add( GTK_CONTAINER( slot->frame ), slot->vbox );

//	slot->image = gtk_drawing_area_new();
//	gtk_box_pack_start( GTK_BOX( slot->vbox ), GTK_WIDGET( slot->image ), FALSE,FALSE, 0 );

	gtk_widget_set_size_request( slot->frame, 32,32 );

//	gtk_widget_show( GTK_WIDGET( slot->image ) );


	slot->title = gtk_label_new(title);
//	slot->timecode = gtk_label_new("");

	gtk_box_pack_start( GTK_BOX( slot->vbox ), GTK_WIDGET( slot->title ), FALSE, FALSE, 0 );
//	gtk_box_pack_start( GTK_BOX( slot->vbox ), GTK_WIDGET( slot->timecode ), FALSE, FALSE, 0 );

	gtk_widget_show_all( slot->frame );
	return slot;
}




static	slot_ui_t	*samplebank_free_slot( void )
{
	int n;
	for( n = 0; n < available_slots_; n ++ )
		if( samplebank_slots_[n]->id == 0 ) return samplebank_slots_[n];
	return NULL;
}


static	slot_ui_t	*samplebank_find_slot( const int id )
{
	int n;
	for( n = 0; n < available_slots_; n ++ )
		if( samplebank_slots_[n]->id == id ) return samplebank_slots_[n];
	return NULL;
}


void	samplebank_store_sample( GtkWidget *notebook, const int id, const char *title )
{
	slot_ui_t	*in_slot = samplebank_find_slot( id );
	slot_ui_t	*free_slot = NULL;

	char new_label[32];
	sprintf(new_label, "%d",id);

	if(!in_slot)
	{
		free_slot = samplebank_free_slot();
		if(!free_slot)
		{
			samplebank_add_page( notebook );
			free_slot = samplebank_free_slot();
		}
		free_slot->id = id;
		gtk_label_set_text( GTK_LABEL( free_slot->title ), new_label );
		//gtk_label_set_text( GTK_LABEL( free_slot->timecode ), "01:01:01:01" );
	}
	else
	{
		if( in_slot->id == id )
		{
			gtk_label_set_text( GTK_LABEL( in_slot->title ), new_label );
		//	gtk_label_set_text( GTK_LABEL( in_slot->timecode ), "01:01:01:01" );
		}
		else
		{
			in_slot->id = id;
			gtk_label_set_text( GTK_LABEL( in_slot->title ), new_label );
		//	gtk_label_set_text( GTK_LABEL( in_slot->timecode ), "01:01:01:01" );
		}

	}
}

void	samplebank_store_in_combobox( GtkWidget *combo_boxA )
{
	//@ Clean up combobox
	if(!combo_boxA)
		return;
	GtkTreeModel *model = gtk_combo_box_get_model( GTK_COMBO_BOX(combo_boxA) );
	gtk_list_store_clear( GTK_LIST_STORE( model ) );
	gtk_combo_box_append_text( GTK_COMBO_BOX( combo_boxA ), "------------" );
	int n;
	for( n = 0; n < available_slots_; n ++ )
	{
		if( samplebank_slots_[n]->id > 0 )
		{
			char text[128];
			snprintf(text,128,"Sample %d", samplebank_slots_[n]->id );
			gtk_combo_box_append_text( GTK_COMBO_BOX( combo_boxA ), text );
		}
	}
	director_lock();	
	gtk_combo_box_set_active( GTK_COMBO_BOX( combo_boxA ), 0 );
	director_unlock();
}
